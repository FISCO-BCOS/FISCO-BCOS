#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-framework/storage2/Storage.h"
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-task/Wait.h>
#include <bcos-transaction-scheduler/ReadWriteSetStorage.h>
#include <fmt/format.h>
#include <boost/test/unit_test.hpp>
#include <type_traits>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::transaction_executor;
using namespace bcos::transaction_scheduler;

class TestReadWriteSetStorageFixture
{
public:
    using Storage =
        memory_storage::MemoryStorage<int, int, memory_storage::Attribute(memory_storage::ORDERED)>;
};

BOOST_FIXTURE_TEST_SUITE(TestReadWriteSetStorage, TestReadWriteSetStorageFixture)

BOOST_AUTO_TEST_CASE(readWriteSet)
{
    task::syncWait([]() -> task::Task<void> {
        Storage lhsStorage;
        ReadWriteSetStorage<decltype(lhsStorage), int> firstStorage(lhsStorage);

        Storage rhsStorage;
        ReadWriteSetStorage<decltype(rhsStorage), int> secondStorage(rhsStorage);

        co_await storage2::writeOne(firstStorage, 100, 1);
        co_await storage2::writeOne(firstStorage, 200, 1);
        co_await storage2::writeOne(firstStorage, 300, 1);

        co_await storage2::readOne(secondStorage, 400);
        co_await storage2::readOne(secondStorage, 500);
        co_await storage2::readOne(secondStorage, 600);

        BOOST_CHECK(!firstStorage.hasRAWIntersection(secondStorage));

        co_await storage2::readOne(secondStorage, 200);
        BOOST_CHECK(firstStorage.hasRAWIntersection(secondStorage));

        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()