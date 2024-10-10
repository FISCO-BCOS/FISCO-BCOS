#include "OpenSSLHasher.h"
#include <memory_resource>

thread_local static std::pmr::unsynchronized_pool_resource sslMemoryPool;  // NOLINT

static void* sslMalloc(size_t size, const char* file, int line)
{
    auto* ptr =
        static_cast<size_t*>(sslMemoryPool.allocate(size + sizeof(size_t), alignof(size_t)));
    *ptr = size;
    return ptr + 1;
}
static void sslFree(void* ptr, const char* file, int line)
{
    auto* sizePtr = static_cast<size_t*>(ptr) - 1;
    sslMemoryPool.deallocate(sizePtr, *sizePtr);
}
static void* sslRealloc(void* ptr, size_t size, const char* file, int line)
{
    auto* oldSizePtr = static_cast<size_t*>(ptr) - 1;
    auto* newPtr = sslMalloc(size, file, line);
    std::copy_n(static_cast<unsigned char*>(ptr), std::min(size, *oldSizePtr),
        static_cast<unsigned char*>(newPtr));
    sslFree(ptr, file, line);
    return newPtr;
}
void bcos::crypto::hasher::openssl::initMallocFunction()
{
    CRYPTO_set_mem_functions(sslMalloc, sslRealloc, sslFree);
}