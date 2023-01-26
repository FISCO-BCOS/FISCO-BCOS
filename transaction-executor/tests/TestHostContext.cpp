// #include "../bcos-transaction-executor/HostContext.h"
#include "../bcos-transaction-executor/vm/HostContext.h"
#include "TestBytecode.h"
#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-framework/storage2/StringPool.h"
#include <bcos-cpp-sdk/utilities/abi/ContractABICodec.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-task/Wait.h>
#include <evmc/evmc.h>
#include <fmt/compile.h>
#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>
#include <iterator>

using namespace bcos::task;
using namespace bcos::storage2;
using namespace bcos::transaction_executor;

constexpr std::string_view helloworldAddress = "HelloWorldHelloWorld";
;
class TestHostContextFixture
{
public:
    TestHostContextFixture()
    {
        bcos::transaction_executor::GlobalHashImpl::g_hashImpl =
            std::make_shared<bcos::crypto::Keccak256>();

        // deploy the hello world contract
        syncWait([this]() -> Task<void> {
            bcostars::protocol::BlockHeaderImpl blockHeader(
                [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });

            std::string helloworldBytecodeBinary;
            boost::algorithm::unhex(
                helloworldBytecode, std::back_inserter(helloworldBytecodeBinary));

            evmc_message message = {.kind = EVMC_CREATE,
                .flags = 0,
                .depth = 0,
                .gas = 1000000,
                .destination = {},
                .destination_ptr = nullptr,
                .destination_len = 0,
                .sender = {},
                .sender_ptr = nullptr,
                .sender_len = 0,
                .input_data = (const uint8_t*)helloworldBytecodeBinary.data(),
                .input_size = helloworldBytecodeBinary.size(),
                .value = {},
                .create2_salt = {}};
            evmc_address origin = {};

            HostContext<decltype(storage)> hostContext(
                storage, tableNamePool, blockHeader, message, origin);
            auto result = co_await hostContext.execute();

            BOOST_CHECK_EQUAL(result.status_code, 0);

            helloworldAddress = result.create_address;
            if (result.release)
            {
                result.release(std::addressof(result));
            }
        }());
    }

    TableNamePool tableNamePool;
    memory_storage::MemoryStorage<StateKey, StateValue, memory_storage::ORDERED> storage;
    evmc_address helloworldAddress;
};

bcos::crypto::Hash::Ptr bcos::transaction_executor::GlobalHashImpl::g_hashImpl;

BOOST_FIXTURE_TEST_SUITE(TestHostContext, TestHostContextFixture)

BOOST_AUTO_TEST_CASE(simpleCall)
{
    syncWait([this]() -> Task<void> {
        bcos::codec::abi::ContractABICodec abiCodec(
            bcos::transaction_executor::GlobalHashImpl::g_hashImpl);
        auto arg = abiCodec.abiIn("getInt()");

        bcostars::protocol::BlockHeaderImpl blockHeader(
            [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
        evmc_message message = {.kind = EVMC_CALL,
            .flags = EVMC_STATIC,
            .depth = 0,
            .gas = 1000000,
            .destination = helloworldAddress,
            .destination_ptr = nullptr,
            .destination_len = 0,
            .sender = {},
            .sender_ptr = nullptr,
            .sender_len = 0,
            .input_data = arg.data(),
            .input_size = arg.size(),
            .value = {},
            .create2_salt = {}};
        evmc_address origin = {};

        HostContext<decltype(storage)> hostContext(
            storage, tableNamePool, blockHeader, message, origin);
        auto result = co_await hostContext.execute();

        BOOST_CHECK_EQUAL(result.status_code, 0);
        bcos::s256 getIntResult = -1;
        abiCodec.abiOut(bcos::bytesConstRef(result.output_data, result.output_size), getIntResult);
        BOOST_CHECK_EQUAL(getIntResult, 0);

        if (result.release)
        {
            result.release(std::addressof(result));
        }

        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()