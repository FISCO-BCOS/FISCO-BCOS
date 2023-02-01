#include <bcos-transaction-scheduler/SchedulerSerialImpl.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::transaction_executor;
using namespace bcos::transaction_scheduler;

class TestSchedulerSerialFixture
{
public:
    using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;
    using BackendStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
        std::hash<StateKey>>;

    TestSchedulerSerialFixture() : levelStorage(backendStorage) {}

    TableNamePool tableNamePool;
    BackendStorage backendStorage;
    MultiLayerStorage<MutableStorage, BackendStorage> levelStorage;
};

BOOST_FIXTURE_TEST_SUITE(TestSchedulerSerial, TestSchedulerSerialFixture)

BOOST_AUTO_TEST_CASE(execute)
{
    BackendStorage backendStorage;
}

BOOST_AUTO_TEST_SUITE_END()