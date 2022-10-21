#include "CacheStorageFactory.h"
#include "StateStorage.h"

using namespace bcos::storage;

MergeableStorageInterface::Ptr CacheStorageFactory::build()
{
    auto cache = std::make_shared<bcos::storage::LRUStateStorage>(m_backendStorage);
    cache->setMaxCapacity(m_cacheSize);
    BCOS_LOG(INFO) << "Build CacheStorage: enableLRUCacheStorage, size: " << m_cacheSize;

    return cache;
}
