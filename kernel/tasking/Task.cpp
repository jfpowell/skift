#include <libsystem/Assert.h>
#include <libsystem/Logger.h>
#include <libsystem/core/CString.h>
#include <libsystem/thread/Atomic.h>

#include "arch/Arch.h"
#include "arch/x86_32/Interrupts.h" /* XXX */
#include "kernel/scheduling/Scheduler.h"
#include "kernel/system/System.h"
#include "kernel/tasking/Task-Handles.h"
#include "kernel/tasking/Task-Memory.h"
#include "kernel/tasking/Task.h"

static int _task_ids = 0;
static List *_tasks;

Task *task_create(Task *parent, const char *name, bool user)
{
    ASSERT_ATOMIC;

    if (_tasks == nullptr)
    {
        _tasks = list_create();
    }

    Task *task = __create(Task);

    task->id = _task_ids++;
    strlcpy(task->name, name, PROCESS_NAME_SIZE);
    task->state = TASK_STATE_NONE;

    if (user)
    {
        task->page_directory = memory_pdir_create();
    }
    else
    {
        task->page_directory = memory_kpdir();
    }

    // Setup shms
    task->memory_mapping = list_create();

    // Setup current working directory.
    lock_init(task->directory_lock);

    if (parent)
    {
        task->directory = path_clone(parent->directory);
    }
    else
    {
        task->directory = path_create("/");
    }

    // Setup fildes
    lock_init(task->handles_lock);
    for (int i = 0; i < PROCESS_HANDLE_COUNT; i++)
    {
        task->handles[i] = nullptr;
    }

    memory_alloc(task->page_directory, PROCESS_STACK_SIZE, MEMORY_CLEAR, (uintptr_t *)&task->kernel_stack);
    task->kernel_stack_pointer = ((uintptr_t)task->kernel_stack + PROCESS_STACK_SIZE);

    memory_map(task->page_directory, MemoryRange(0xff000000, PROCESS_STACK_SIZE), MEMORY_USER);
    task->user_stack_pointer = 0xff000000 + PROCESS_STACK_SIZE;
    task->user_stack = (void *)0xff000000;

    arch_save_context(task);

    list_pushback(_tasks, task);

    return task;
}

void task_destroy(Task *task)
{
    atomic_begin();

    if (task->state != TASK_STATE_NONE)
        task_set_state(task, TASK_STATE_NONE);

    list_remove(_tasks, task);

    atomic_end();

    MemoryMapping *mapping = nullptr;

    while ((mapping = (MemoryMapping *)list_peek(task->memory_mapping)))
    {
        task_memory_mapping_destroy(task, mapping);
    }

    list_destroy(task->memory_mapping);

    task_fshandle_close_all(task);

    path_destroy(task->directory);

    memory_free(task->page_directory, MemoryRange{(uintptr_t)task->kernel_stack, PROCESS_STACK_SIZE});
    memory_free(task->page_directory, MemoryRange{(uintptr_t)task->user_stack, PROCESS_STACK_SIZE});

    if (task->page_directory != memory_kpdir())
    {
        memory_pdir_destroy(task->page_directory);
    }

    free(task);
}

void task_iterate(void *target, TaskIterateCallback callback)
{
    AtomicHolder holder;

    if (!_tasks)
    {
        return;
    }

    list_iterate(_tasks, target, (ListIterationCallback)callback);
}

Task *task_by_id(int id)
{
    list_foreach(Task, task, _tasks)
    {
        if (task->id == id)
            return task;
    }

    return nullptr;
}

int task_count()
{
    AtomicHolder holder;

    if (!_tasks)
    {
        return 0;
    }

    return _tasks->count();
}

Task *task_spawn(Task *parent, const char *name, TaskEntryPoint entry, void *arg, bool user)
{
    ASSERT_ATOMIC;

    Task *task = task_create(parent, name, user);

    task_set_entry(task, entry, user);
    task_kernel_stack_push(task, &arg, sizeof(arg));

    return task;
}

static void pass_argc_argv_user(Task *task, const char **argv)
{
    PageDirectory *parent_pdir = task_switch_pdir(scheduler_running(), task->page_directory);

    uintptr_t argv_list[PROCESS_ARG_COUNT] = {};

    int argc;
    for (argc = 0; argv[argc] && argc < PROCESS_ARG_COUNT; argc++)
    {
        argv_list[argc] = task_user_stack_push(task, argv[argc], strlen(argv[argc]) + 1);
    }

    uintptr_t argv_list_ref = task_user_stack_push(task, &argv_list, sizeof(argv_list));

    task_user_stack_push(task, &argv_list_ref, sizeof(argv_list_ref));
    task_user_stack_push(task, &argc, sizeof(argc));

    task_switch_pdir(scheduler_running(), parent_pdir);
}

static void pass_argc_argv_kernel(Task *task, const char **argv)
{
    uintptr_t argv_list[PROCESS_ARG_COUNT] = {};

    int argc;
    for (argc = 0; argv[argc] && argc < PROCESS_ARG_COUNT; argc++)
    {
        argv_list[argc] = task_kernel_stack_push(task, argv[argc], strlen(argv[argc]) + 1);
    }

    uintptr_t argv_list_ref = task_kernel_stack_push(task, &argv_list, sizeof(argv_list));

    task_kernel_stack_push(task, &argv_list_ref, sizeof(argv_list_ref));
    task_kernel_stack_push(task, &argc, sizeof(argc));
}

Task *task_spawn_with_argv(Task *parent, const char *name, TaskEntryPoint entry, const char **argv, bool user)
{
    AtomicHolder holder;

    Task *task = task_create(parent, name, user);

    task_set_entry(task, entry, true);

    if (user)
    {
        pass_argc_argv_user(task, argv);
    }
    else
    {
        pass_argc_argv_kernel(task, argv);
    }

    return task;
}

void task_set_state(Task *task, TaskState state)
{
    ASSERT_ATOMIC;

    scheduler_did_change_task_state(task, task->state, state);
    task->state = state;
}

void task_set_entry(Task *task, TaskEntryPoint entry, bool user)
{
    task->entry_point = entry;
    task->user = user;
}

uintptr_t task_kernel_stack_push(Task *task, const void *value, size_t size)
{
    task->kernel_stack_pointer -= size;
    memcpy((void *)task->kernel_stack_pointer, value, size);

    return task->kernel_stack_pointer;
}

uintptr_t task_user_stack_push(Task *task, const void *value, size_t size)
{
    task->user_stack_pointer -= size;
    memcpy((void *)task->user_stack_pointer, value, size);
    return task->user_stack_pointer;
}

void task_go(Task *task)
{
    if (task->user)
    {
        UserInterruptStackFrame stackframe = {};

        stackframe.user_esp = task->user_stack_pointer;

        stackframe.eflags = 0x202;
        stackframe.eip = (uintptr_t)task->entry_point;
        stackframe.ebp = 0;

        stackframe.cs = 0x1b;
        stackframe.ds = 0x23;
        stackframe.es = 0x23;
        stackframe.fs = 0x23;
        stackframe.gs = 0x23;
        stackframe.ss = 0x23;

        task_kernel_stack_push(task, &stackframe, sizeof(UserInterruptStackFrame));
    }
    else
    {
        InterruptStackFrame stackframe = {};

        stackframe.eflags = 0x202;
        stackframe.eip = (uintptr_t)task->entry_point;
        stackframe.ebp = 0;

        stackframe.cs = 0x08;
        stackframe.ds = 0x10;
        stackframe.es = 0x10;
        stackframe.fs = 0x10;
        stackframe.gs = 0x10;

        task_kernel_stack_push(task, &stackframe, sizeof(InterruptStackFrame));
    }

    atomic_begin();
    task_set_state(task, TASK_STATE_RUNNING);
    atomic_end();
}

Result task_sleep(Task *task, int timeout)
{
    task_block(task, new BlockerTime(system_get_tick() + timeout), -1);

    return TIMEOUT;
}

Result task_wait(int task_id, int *exit_value)
{
    AtomicHolder holder;

    Task *task = task_by_id(task_id);

    if (!task)
    {
        return ERR_NO_SUCH_TASK;
    }

    task_block(scheduler_running(), new BlockerWait(task, exit_value), -1);

    return SUCCESS;
}

BlockerResult task_block(Task *task, Blocker *blocker, Timeout timeout)
{
    assert(!task->blocker);

    atomic_begin();
    task->blocker = blocker;
    if (blocker->can_unblock(task))
    {
        blocker->on_unblock(task);

        atomic_end();

        task->blocker = nullptr;
        delete blocker;

        return BLOCKER_UNBLOCKED;
    }

    if (timeout == (Timeout)-1)
    {
        blocker->_timeout = (Timeout)-1;
    }
    else
    {
        blocker->_timeout = system_get_tick() + timeout;
    }

    task_set_state(task, TASK_STATE_BLOCKED);
    atomic_end();

    scheduler_yield();

    BlockerResult result = blocker->_result;

    task->blocker = nullptr;
    delete blocker;

    return result;
}

Result task_cancel(Task *task, int exit_value)
{
    assert(task);

    AtomicHolder holder;

    task->exit_value = exit_value;
    task_set_state(task, TASK_STATE_CANCELED);

    return SUCCESS;
}

void task_exit(int exit_value)
{
    task_cancel(scheduler_running(), exit_value);

    scheduler_yield();

    ASSERT_NOT_REACHED();
}

void task_dump(Task *task)
{
    if (!task)
        return;

    AtomicHolder holder;

    printf("\n\t - Task %d %s", task->id, task->name);
    printf("\n\t   State: %s", task_state_string(task->state));
    printf("\n\t   Memory: ");
    memory_pdir_dump(task->page_directory, false);

    if (task->page_directory == memory_kpdir())
    {
        printf("\n\t   Page directory: %08x (kpdir)", task->page_directory);
    }
    else
    {
        printf("\n\t   Page directory: %08x", task->page_directory);
    }

    printf("\n");
}
