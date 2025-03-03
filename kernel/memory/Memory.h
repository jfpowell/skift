#pragma once

#include <abi/Memory.h>
#include <libsystem/Result.h>

#include "arch/x86_32/Paging.h"
#include "kernel/handover/Handover.h"
#include "kernel/memory/MemoryRange.h"

void memory_initialize(Handover *handover);

void memory_dump();

size_t memory_get_used();

size_t memory_get_total();

PageDirectory *memory_kpdir();

Result memory_map(PageDirectory *page_directory, MemoryRange range, MemoryFlags flags);

Result memory_map_identity(PageDirectory *page_directory, MemoryRange range, MemoryFlags flags);

Result memory_alloc(PageDirectory *page_directory, size_t size, MemoryFlags flags, uintptr_t *out_address);

Result memory_alloc_identity(PageDirectory *page_directory, MemoryFlags flags, uintptr_t *out_address);

Result memory_free(PageDirectory *page_directory, MemoryRange range);

PageDirectory *memory_pdir_create();

void memory_pdir_destroy(PageDirectory *pdir);

void memory_pdir_dump(PageDirectory *pdir, bool user);

void memory_pdir_switch(PageDirectory *pdir);

void memory_map_me_later(MemoryRange range);
