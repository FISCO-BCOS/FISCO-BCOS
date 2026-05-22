#include "GlobalStateStorageInitializer.h"

bcos::initializer::GlobalStateStorageInitializer::GlobalStateStorageInitializer(
    std::string const& storageRootPath)
  : m_checkpointStorage(storageRootPath,
        storage2::rocksdb::StateKeyResolver{}, storage2::rocksdb::StateValueResolver{}),
    m_storage(m_checkpointStorage, m_cacheStorage)
{}

bcos::initializer::GlobalStateStorageInitializer::Ptr
bcos::initializer::GlobalStateStorageInitializer::build(std::string const& storageRootPath)
{
    return std::make_shared<GlobalStateStorageInitializer>(storageRootPath);
}