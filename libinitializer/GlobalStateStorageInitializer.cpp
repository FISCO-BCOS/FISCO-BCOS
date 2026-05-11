#include "GlobalStateStorageInitializer.h"

bcos::initializer::GlobalStateStorageInitializer::GlobalStateStorageInitializer(
    ::rocksdb::DB& rocksDB)
  : m_rocksDBStorage(rocksDB, storage2::rocksdb::StateKeyResolver{},
        storage2::rocksdb::StateValueResolver{}),
    m_storage(m_rocksDBStorage, m_cacheStorage)
{}

bcos::initializer::GlobalStateStorageInitializer::Ptr
bcos::initializer::GlobalStateStorageInitializer::build(::rocksdb::DB& rocksDB)
{
    return std::make_shared<GlobalStateStorageInitializer>(rocksDB);
}