#pragma once

#include <bcos-table/src/StateStorageFactory.h>
#include <tbb/concurrent_unordered_map.h>
#include <map>

using namespace bcos;
using namespace bcos::storage;

namespace  bcos::test
{
class MockStateStorageFactory  : public bcos::storage::StateStorageFactory
{
public:
    using Ptr = std::shared_ptr<MockStateStorageFactory>;
    MockStateStorageFactory(size_t keyPageSize) : StateStorageFactory(keyPageSize) {}
    virtual ~MockStateStorageFactory() {};

    storage::StateStorageInterface::Ptr createStateStorage (
        bcos::storage::StorageInterface::Ptr, uint32_t ) override
    { return nullptr;}


};
}
