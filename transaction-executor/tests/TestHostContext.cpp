// #include "../bcos-transaction-executor/HostContext.h"
#include "../bcos-transaction-executor/vm/HostContext.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::task;
using namespace bcos::storage2;
using namespace bcos::transaction_executor;

class TestHostContextFixture
{
};

BOOST_FIXTURE_TEST_SUITE(TestHostContext, TestHostContextFixture)

BOOST_AUTO_TEST_CASE(simpleCall)
{
    syncWait([]() -> Task<void> {
        memory_storage::MemoryStorage<StateKey, StateValue> storage;
        TableNamePool tableNamePool;
        bcostars::protocol::BlockHeaderImpl blockHeader(
            [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
        evmc_message message = {.kind = EVMC_CALL,
            .flags = 0,
            .depth = 0,
            .gas = 1000000,
            .destination = {},
            .destination_ptr = nullptr,
            .destination_len = 0,
            .sender = {},
            .sender_ptr = nullptr,
            .sender_len = 0,
            .input_data = nullptr,
            .input_size = 0,
            .value = {},
            .create2_salt = {}};

        HostContext<decltype(storage)> hostContext(storage, tableNamePool, blockHeader, message);

        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()