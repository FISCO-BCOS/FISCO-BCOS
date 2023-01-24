// #include "../bcos-transaction-executor/HostContext.h"
#include "../bcos-transaction-executor/vm/HostContext.h"
#include "TestBytecode.h"
#include "bcos-framework/storage2/StringPool.h"
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-task/Wait.h>
#include <boost/test/unit_test.hpp>

using namespace bcos::task;
using namespace bcos::storage2;
using namespace bcos::transaction_executor;

constexpr std::string_view helloworldAddress = "HelloWorld";
;
class TestHostContextFixture
{
public:
    TestHostContextFixture()
    {
        // Load helloworld contract bytecode
        syncWait([](decltype(this) self) -> Task<void> {
            auto tableName = std::string("/apps/") + std::string(helloworldAddress);
            bcos::storage::Entry codeEntry;
            codeEntry.set(helloworldBytecode);

            co_await writeOne(self->storage,
                StateKey{string_pool::makeStringID(self->tableNamePool, tableName), ACCOUNT_CODE},
                std::move(codeEntry));
            co_return;
        }(this));
    }

    TableNamePool tableNamePool;
    memory_storage::MemoryStorage<StateKey, StateValue, memory_storage::ORDERED> storage;
};

bcos::crypto::Hash::Ptr bcos::transaction_executor::GlobalHashImpl::g_hashImpl;

BOOST_FIXTURE_TEST_SUITE(TestHostContext, TestHostContextFixture)

BOOST_AUTO_TEST_CASE(simpleCall)
{
    syncWait([this]() -> Task<void> {
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
        RANGES::copy_n(
            helloworldAddress.data(), helloworldAddress.size(), message.destination.bytes);

        evmc_address origin = {};

        HostContext<decltype(storage)> hostContext(
            storage, tableNamePool, blockHeader, message, origin);

        auto result = co_await hostContext.execute();

        std::cout << "execute done!" << std::endl;
        BOOST_CHECK_EQUAL(result.status_code, 0);

        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()