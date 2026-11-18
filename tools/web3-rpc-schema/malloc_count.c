/* SPDX-License-Identifier: Apache-2.0 */
/* Optional macOS allocation-event counter for the X7 acceptance profile. */
#include <fcntl.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

static _Atomic uint64_t* allocation_count;

__attribute__((constructor)) static void init_counter(void)
{
    char const* path = getenv("X7_MALLOC_COUNT_FILE");
    if (!path)
        return;
    int fd = open(path, O_RDWR);
    if (fd == -1)
        return;
    void* memory = mmap(NULL, sizeof(uint64_t), PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    if (memory != MAP_FAILED)
        allocation_count = memory;
}

static void record_allocation(void* result)
{
    if (result && allocation_count)
        atomic_fetch_add_explicit(allocation_count, 1, memory_order_relaxed);
}

#define COUNT_ALLOCATIONS(result_type, name, signature, arguments)     \
    static result_type counted_##name signature                        \
    {                                                                  \
        result_type result = name arguments;                           \
        record_allocation(result);                                     \
        return result;                                                 \
    }                                                                  \
    __attribute__((used, section("__DATA,__interpose"))) static struct \
    {                                                                  \
        void const* replacement;                                       \
        void const* original;                                          \
    } const interpose_##name = {counted_##name, name};

COUNT_ALLOCATIONS(void*, malloc, (size_t length), (length))
COUNT_ALLOCATIONS(void*, calloc, (size_t count, size_t length), (count, length))
COUNT_ALLOCATIONS(void*, realloc, (void* buffer, size_t length), (buffer, length))
COUNT_ALLOCATIONS(void*, reallocf, (void* buffer, size_t length), (buffer, length))
COUNT_ALLOCATIONS(void*, aligned_alloc, (size_t alignment, size_t length), (alignment, length))
COUNT_ALLOCATIONS(void*, valloc, (size_t length), (length))

static int counted_posix_memalign(void** result, size_t alignment, size_t length)
{
    int status = posix_memalign(result, alignment, length);
    if (status == 0)
        record_allocation(*result);
    return status;
}

__attribute__((used, section("__DATA,__interpose"))) static struct
{
    void const* replacement;
    void const* original;
} const interpose_posix_memalign = {counted_posix_memalign, posix_memalign};
