#include "../bcos-transaction-scheduler/LevelStorage.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include <boost/test/unit_test.hpp>
#include <type_traits>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::transaction_executor;
using namespace bcos::transaction_scheduler;

class TestLevelStorageFixture
{
};

BOOST_FIXTURE_TEST_SUITE(TestLevelStorage, TestLevelStorageFixture)

BOOST_AUTO_TEST_CASE(setAndGet)
{
    using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED)>;
    using BackendStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED)>;

    constexpr static bool isVoid = std::is_void_v<BackendStorage>;

    BackendStorage backendStorage;
    LevelStorage<MutableStorage, BackendStorage> levelStorage(backendStorage);
}

BOOST_AUTO_TEST_SUITE_END()