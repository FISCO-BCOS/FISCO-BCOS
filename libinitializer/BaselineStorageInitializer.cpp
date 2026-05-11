#include "BaselineStorageInitializer.h"

bcos::initializer::BaselineStorageInitializer::BaselineStorageInitializer(::rocksdb::DB& rocksDB)
  : m_rocksDBStorage(rocksDB, storage2::rocksdb::StateKeyResolver{},
        storage2::rocksdb::StateValueResolver{}),
    m_multiLayerStorage(m_rocksDBStorage, m_cacheStorage)
{}

bcos::initializer::BaselineStorageInitializer::Ptr
bcos::initializer::BaselineStorageInitializer::build(::rocksdb::DB& rocksDB)
{
    return std::make_shared<BaselineStorageInitializer>(rocksDB);
}