// #include "../bcos-transaction-executor/HostContext.h"
#include "../bcos-transaction-executor/vm/HostContext.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <tbb/concurrent_map.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::storage2;
using namespace bcos::transaction_executor;

class TestHostContextFixture
{
};

BOOST_FIXTURE_TEST_SUITE(TestHostContext, TestHostContextFixture)

BOOST_AUTO_TEST_CASE(normalOP)
{
    memory_storage::MemoryStorage<StateKey, StateValue> storage;
    HostContext<decltype(storage)> hostContext;
}

BOOST_AUTO_TEST_SUITE_END()