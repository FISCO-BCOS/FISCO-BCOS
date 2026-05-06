#include "CacheStorageFactory.h"
#include "StateStorage.h"

using namespace bcos::storage;

CacheStorageFactory::CacheStorageFactory(
        bcos::storage::TransactionalStorageInterface::Ptr backendStorage, ssize_t cacheSize)
    : m_cacheSize(cacheSize), m_backendStorage(std::move(backendStorage))
{}

MergeableStorageInterface::Ptr CacheStorageFactory::build()
{
    auto cache = std::make_shared<bcos::storage::LRUStateStorage>(m_backendStorage, false);
    cache->setMaxCapacity(m_cacheSize);
    BCOS_LOG(INFO) << "Build CacheStorage: enableLRUCacheStorage, size: " << m_cacheSize;

    return cache;
}
