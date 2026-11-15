#ifndef TEST_KERNEL_H
#define TEST_KERNEL_H
#include <stddef.h>
#include <stdint.h>
#define PAGE_SIZE 4096u
#define PAGE_READWRITE 4u
void *MmAllocateContiguousMemoryEx(size_t bytes, uintptr_t low, uintptr_t high,
                                  size_t alignment, unsigned protection);
void MmFreeContiguousMemory(void *pointer);
#endif
