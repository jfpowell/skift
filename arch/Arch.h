#pragma once

#include "kernel/tasking/Task.h"

void arch_initialize();

void arch_disable_interrupts();

void arch_enable_interrupts();

void arch_halt();

void arch_yield();

void arch_save_context(Task *task);

void arch_load_context(Task *task);

size_t arch_debug_write(const void *buffer, size_t size);

TimeStamp arch_get_time();

__no_return void arch_reboot();

__no_return void arch_shutdown();
