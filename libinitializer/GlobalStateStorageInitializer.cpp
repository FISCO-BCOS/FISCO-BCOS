#include "GlobalStateStorageInitializer.h"

bcos::initializer::GlobalStateStorageInitializer::GlobalStateStorageInitializer(
    std::string const& storageRootPath,
    bcos::storage2::rocksdb::RocksDBCheckpointOption const& rocksDBOption)
  : m_checkpointStorage(storageRootPath,
        storage2::rocksdb::StateKeyResolver{}, storage2::rocksdb::StateValueResolver{},
        rocksDBOption),
    m_storage(m_checkpointStorage, m_cacheStorage)
{}

bcos::initializer::GlobalStateStorageInitializer::Ptr
bcos::initializer::GlobalStateStorageInitializer::build(
    std::string const& storageRootPath,
    bcos::storage2::rocksdb::RocksDBCheckpointOption const& rocksDBOption)
{
    return std::make_shared<GlobalStateStorageInitializer>(storageRootPath, rocksDBOption);
}