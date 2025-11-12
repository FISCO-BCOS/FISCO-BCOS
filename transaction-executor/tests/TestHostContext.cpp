#include "../bcos-transaction-executor/precompiled/PrecompiledManager.h"
#include "../bcos-transaction-executor/vm/HostContext.h"
#include "TestBytecode.h"
#include "TestMemoryStorage.h"
#include "bcos-codec/bcos-codec/abi/ContractABICodec.h"
#include "bcos-crypto/interfaces/crypto/CryptoSuite.h"
#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/GenesisConfig.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-ledger/Ledger.h"
#include "bcos-table/src/LegacyStorageWrapper.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-task/Wait.h"
#include "bcos-tool/VersionConverter.h"
#include "bcos-transaction-executor/RollbackableStorage.h"
#include "bcos-utilities/FixedBytes.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <evmc/evmc.h>
#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <atomic>
#include <iterator>
#include <memory>

using namespace bcos::task;
using namespace bcos::storage2;
using namespace bcos::executor_v1;
using namespace bcos::executor_v1::hostcontext;

class TestHostContextFixture
{
public:
    bcos::crypto::Hash::Ptr hashImpl = std::make_shared<bcos::crypto::Keccak256>();
    MutableStorage storage;
    Rollbackable<decltype(storage)> rollbackableStorage;
    using MemoryStorageType =
        bcos::storage2::memory_storage::MemoryStorage<bcos::executor_v1::StateKey,
            bcos::executor_v1::StateValue,
            bcos::storage2::memory_storage::Attribute(
                bcos::storage2::memory_storage::ORDERED |
                bcos::storage2::memory_storage::LOGICAL_DELETION)>;
    MemoryStorageType transientStorage;
    Rollbackable<MemoryStorageType> rollbackableTransientStorage;
    evmc_address helloworldAddress{};
    int64_t seq = 0;
    std::optional<PrecompiledManager> precompiledManager;
    bcos::ledger::LedgerConfig ledgerConfig;
    bcostars::protocol::BlockHeaderImpl blockHeader;

    TestHostContextFixture()
      : rollbackableStorage(storage), rollbackableTransientStorage(transientStorage)
    {
        bcos::executor::GlobalHashImpl::g_hashImpl = std::make_shared<bcos::crypto::Keccak256>();
        precompiledManager.emplace(hashImpl);

        // deploy the hello world contract
        blockHeader.setVersion(static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION));
        blockHeader.calculateHash(*hashImpl);

        std::string helloworldBytecodeBinary;
        boost::algorithm::unhex(helloworldBytecode, std::back_inserter(helloworldBytecodeBinary));

        evmc_message message = {.kind = EVMC_CREATE,
            .flags = 0,
            .depth = 0,
            .gas = 300 * 10000,
            .recipient = {},
            .destination_ptr = nullptr,
            .destination_len = 0,
            .sender = {},
            .sender_ptr = nullptr,
            .sender_len = 0,
            .input_data = (const uint8_t*)helloworldBytecodeBinary.data(),
            .input_size = helloworldBytecodeBinary.size(),
            .value = {},
            .create2_salt = {},
            .code_address = {}};
        evmc_address origin = {};

        HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)>
            hostContext(rollbackableStorage, rollbackableTransientStorage, blockHeader, message,
                origin, "", 0, seq, *precompiledManager, ledgerConfig, *hashImpl, false, 0,
                bcos::task::syncWait);
        syncWait(hostContext.prepare());
        auto result = syncWait(hostContext.execute());
        BOOST_REQUIRE_EQUAL(result.status_code, 0);

        helloworldAddress = result.create_address;
        BCOS_LOG(INFO) << "Hello world address: " << bcos::address2HexString(helloworldAddress);
    }

    Task<EVMCResult> call(const evmc_address& address, std::string_view abi, evmc_address sender,
        bool web3, auto&&... args)
    {
        bcos::codec::abi::ContractABICodec abiCodec(*bcos::executor::GlobalHashImpl::g_hashImpl);
        auto input = abiCodec.abiIn(std::string(abi), std::forward<decltype(args)>(args)...);

        static std::atomic_int64_t number = 0;
        blockHeader.setNumber(number++);
        blockHeader.calculateHash(*hashImpl);

        evmc_message message = {.kind = EVMC_CALL,
            .flags = 0,
            .depth = 0,
            .gas = 1000000,
            .recipient = address,
            .destination_ptr = nullptr,
            .destination_len = 0,
            .sender = sender,
            .sender_ptr = nullptr,
            .sender_len = 0,
            .input_data = input.data(),
            .input_size = input.size(),
            .value = {},
            .create2_salt = {},
            .code_address = address};
        evmc_address origin = {};

        HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)>
            hostContext(rollbackableStorage, rollbackableTransientStorage, blockHeader, message,
                origin, "", 0, seq, *precompiledManager, ledgerConfig, *hashImpl, web3, 0,
                bcos::task::syncWait);
        co_await hostContext.prepare();
        auto result = co_await hostContext.execute();

        co_return result;
    }

    Task<EVMCResult> call(const bcos::Address& address, std::string_view abi, evmc_address sender,
        bool web3, auto&&... args)
    {
        evmc_address evmcAddress{};
        std::copy(address.begin(), address.end(), evmcAddress.bytes);
        co_return co_await call(
            evmcAddress, abi, sender, web3, std::forward<decltype(args)>(args)...);
    }

    Task<EVMCResult> call(std::string_view abi, evmc_address sender, bool web3, auto&&... args)
    {
        co_return co_await call(
            helloworldAddress, abi, sender, web3, std::forward<decltype(args)>(args)...);
    }

    bcos::task::Task<void> initBFS(
        const bcos::protocol::BlockHeader& header, bcos::crypto::Hash& hashImpl)
    {
        bcos::codec::abi::ContractABICodec abiCodec(hashImpl);
        auto input = abiCodec.abiIn("initBfs()");
        auto address = bcos::Address(0x100e);
        evmc_address callAddress{};
        ::ranges::copy(address, callAddress.bytes);
        evmc_message message = {.kind = EVMC_CALL,
            .flags = 0,
            .depth = 0,
            .gas = 1000000,
            .recipient = callAddress,
            .destination_ptr = nullptr,
            .destination_len = 0,
            .sender = {},
            .sender_ptr = nullptr,
            .sender_len = 0,
            .input_data = input.data(),
            .input_size = input.size(),
            .value = {},
            .create2_salt = {},
            .code_address = callAddress};
        evmc_address origin = {};

        HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)>
            hostContext(rollbackableStorage, rollbackableTransientStorage, header, message, origin,
                "", 0, seq, *precompiledManager, ledgerConfig, hashImpl, false, 0,
                bcos::task::syncWait);
        co_await hostContext.prepare();
        BOOST_CHECK_NO_THROW(auto result = co_await hostContext.execute());
    }
};

BOOST_FIXTURE_TEST_SUITE(TestHostContext, TestHostContextFixture)

BOOST_AUTO_TEST_CASE(bits)
{
    auto evmAddress = bcos::unhexAddress("0x0000000000000000000000000000000000000100");
    bcos::u160 address1;
    boost::multiprecision::import_bits(
        address1, evmAddress.bytes, evmAddress.bytes + sizeof(evmAddress.bytes));
    auto address2 =
        fromBigEndian<bcos::u160>(bcos::bytesConstRef(evmAddress.bytes, sizeof(evmAddress.bytes)));

    BOOST_CHECK_EQUAL(address1, address2);
}

BOOST_AUTO_TEST_CASE(simpleCall)
{
    syncWait([this]() -> Task<void> {
        auto result = co_await call("getInt()", {}, false);

        BOOST_CHECK_EQUAL(result.status_code, 0);
        bcos::s256 getIntResult = -1;
        bcos::codec::abi::ContractABICodec abiCodec(*bcos::executor::GlobalHashImpl::g_hashImpl);
        abiCodec.abiOut(bcos::bytesConstRef(result.output_data, result.output_size), getIntResult);
        BOOST_CHECK_EQUAL(getIntResult, 0);
    }());
}

BOOST_AUTO_TEST_CASE(executeAndCall)
{
    syncWait([this]() -> Task<void> {
        auto result1 = co_await call("setInt(int256)", {}, false, bcos::s256(10000));
        auto result2 = co_await call("getInt()", {}, false);
        auto result3 =
            co_await call("setString(string)", {}, false, std::string("Hello world, fisco-bcos!"));
        auto result4 = co_await call("getString()", {}, false);

        BOOST_CHECK_EQUAL(result1.status_code, 0);
        BOOST_CHECK_EQUAL(result2.status_code, 0);
        BOOST_CHECK_EQUAL(result3.status_code, 0);
        BOOST_CHECK_EQUAL(result4.status_code, 0);
        bcos::s256 getIntResult = -1;
        bcos::codec::abi::ContractABICodec abiCodec(*bcos::executor::GlobalHashImpl::g_hashImpl);
        abiCodec.abiOut(
            bcos::bytesConstRef(result2.output_data, result2.output_size), getIntResult);
        BOOST_CHECK_EQUAL(getIntResult, 10000);

        std::string out;
        abiCodec.abiOut(bcos::bytesConstRef(result4.output_data, result4.output_size), out);
        BOOST_CHECK_EQUAL(out, "Hello world, fisco-bcos!");
    }());
}

BOOST_AUTO_TEST_CASE(contractDeploy)
{
    syncWait([this]() -> Task<void> {
        auto result = co_await call("deployAndCall(int256)", {}, false, bcos::s256(999));

        BOOST_CHECK_EQUAL(result.status_code, 0);
        bcos::s256 getIntResult = -1;
        bcos::codec::abi::ContractABICodec abiCodec(*bcos::executor::GlobalHashImpl::g_hashImpl);
        abiCodec.abiOut(bcos::bytesConstRef(result.output_data, result.output_size), getIntResult);
        BOOST_CHECK_EQUAL(getIntResult, 999);
    }());
}

BOOST_AUTO_TEST_CASE(createTwice)
{
    syncWait([this]() -> Task<void> {
        auto result = co_await call("createTwice()", {}, false);
        BOOST_CHECK_EQUAL(result.status_code, 0);

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(emptyCreate)
{
    // Create a contract with empty init code and zero value
    syncWait([this]() -> Task<void> {
        // Advance block height
        blockHeader.setNumber(seq++);
        blockHeader.calculateHash(*hashImpl);

        constexpr int64_t CREATE_GAS = 300LL * 10000LL;  // gas budget for empty create
        evmc_message message = {.kind = EVMC_CREATE,
            .flags = 0,
            .depth = 0,
            .gas = CREATE_GAS,
            .recipient = {},
            .destination_ptr = nullptr,
            .destination_len = 0,
            .sender = {},
            .sender_ptr = nullptr,
            .sender_len = 0,
            .input_data = nullptr,
            .input_size = 0,
            .value = {},  // zero value
            .create2_salt = {},
            .code_address = {}};

        evmc_address origin{};
        HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)>
            hostContext(rollbackableStorage, rollbackableTransientStorage, blockHeader, message,
                origin, "", 0, seq, *precompiledManager, ledgerConfig, *hashImpl, false, 0,
                bcos::task::syncWait);

        co_await hostContext.prepare();
        auto result = co_await hostContext.execute();

        BOOST_CHECK_EQUAL(result.status_code, EVMC_SUCCESS);
        auto size = co_await hostContext.codeSizeAt(result.create_address);
        BOOST_CHECK_EQUAL(size, 0);

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(failure)
{
    syncWait([this]() -> Task<void> {
        bcos::codec::abi::ContractABICodec abiCodec(*bcos::executor::GlobalHashImpl::g_hashImpl);

        auto result1 = co_await call("returnRequire()", {}, false);
        BOOST_CHECK_EQUAL(result1.status_code, 2);

        auto result2 = co_await call("getInt()", {}, false);
        BOOST_CHECK_EQUAL(result2.status_code, 0);
        bcos::s256 getIntResult = -1;
        abiCodec.abiOut(
            bcos::bytesConstRef(result2.output_data, result2.output_size), getIntResult);
        BOOST_CHECK_EQUAL(getIntResult, 0);

        auto result3 = co_await call("returnRevert()", {}, false);
        BOOST_CHECK_EQUAL(result3.status_code, 2);

        auto result4 = co_await call("getInt()", {}, false);
        BOOST_CHECK_EQUAL(result4.status_code, 0);
        abiCodec.abiOut(
            bcos::bytesConstRef(result4.output_data, result4.output_size), getIntResult);
        BOOST_CHECK_EQUAL(getIntResult, 0);
    }());
}

BOOST_AUTO_TEST_CASE(delegateCall)
{
    syncWait([this]() -> Task<void> {
        bcos::codec::abi::ContractABICodec abiCodec(*bcos::executor::GlobalHashImpl::g_hashImpl);

        evmc_address sender = bcos::unhexAddress("0x0000000000000000000000000000000000000050");
        auto result1 = co_await call("delegateCall()", sender, false);
        BOOST_CHECK_EQUAL(result1.status_code, 0);

        auto result2 = co_await call("getInt()", sender, false);
        bcos::s256 getIntResult = -1;
        abiCodec.abiOut(
            bcos::bytesConstRef(result2.output_data, result2.output_size), getIntResult);
        BOOST_CHECK_EQUAL(getIntResult, 19876);

        auto result3 = co_await call("getString()", sender, false);
        std::string strResult;
        abiCodec.abiOut(bcos::bytesConstRef(result3.output_data, result3.output_size), strResult);
        BOOST_CHECK_EQUAL(strResult, "hi!");
    }());
}

BOOST_AUTO_TEST_CASE(precompiled)
{
    // Use ledger to init storage
    auto ledgerConfig = bcos::ledger::LedgerConfig{};
    auto storageWrapper =
        std::make_shared<bcos::storage::LegacyStorageWrapper<std::decay_t<decltype(storage)>>>(
            storage);
    auto cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    bcos::ledger::Ledger ledger(
        std::make_shared<bcostars::protocol::BlockFactoryImpl>(cryptoSuite,
            std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(cryptoSuite),
            std::make_shared<bcostars::protocol::TransactionFactoryImpl>(cryptoSuite),
            std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite)),
        storageWrapper, 1000);
    bcos::ledger::GenesisConfig genesis;
    genesis.m_txGasLimit = 100000;
    genesis.m_compatibilityVersion = bcos::tool::toVersionNumber("3.6.0");
    ledger.buildGenesisBlock(genesis, ledgerConfig);

    bcostars::protocol::BlockHeaderImpl blockHeader;
    blockHeader.inner().data.version = (int)bcos::protocol::BlockVersion::V3_5_VERSION;
    blockHeader.calculateHash(*bcos::executor::GlobalHashImpl::g_hashImpl);

    syncWait(initBFS(blockHeader, *hashImpl));

    bcos::codec::abi::ContractABICodec abiCodec(*bcos::executor::GlobalHashImpl::g_hashImpl);
    std::optional<EVMCResult> result;
    {
        auto input = abiCodec.abiIn(std::string("makeShard(string)"), std::string("shared1"));

        auto address = bcos::Address(0x1010);
        evmc_address callAddress{};
        std::uninitialized_copy(address.begin(), address.end(), callAddress.bytes);
        evmc_message message = {.kind = EVMC_CALL,
            .flags = 0,
            .depth = 0,
            .gas = 1000000,
            .recipient = callAddress,
            .destination_ptr = nullptr,
            .destination_len = 0,
            .sender = {},
            .sender_ptr = nullptr,
            .sender_len = 0,
            .input_data = input.data(),
            .input_size = input.size(),
            .value = {},
            .create2_salt = {},
            .code_address = callAddress};
        evmc_address origin = {};

        HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)>
            hostContext(rollbackableStorage, rollbackableTransientStorage, blockHeader, message,
                origin, "", 0, seq, *precompiledManager, ledgerConfig, *hashImpl, false, 0,
                bcos::task::syncWait);
        syncWait(hostContext.prepare());

        auto notFoundResult = syncWait(hostContext.execute());
        BOOST_CHECK_EQUAL(notFoundResult.status_code, EVMC_REVERT);

        bcos::codec::abi::ContractABICodec abi(*hashImpl);
        std::string errorMessage;
        BOOST_REQUIRE_GT(notFoundResult.output_size, 4);
        abi.abiOut({notFoundResult.output_data + 4, notFoundResult.output_size - 4}, errorMessage);
        BOOST_CHECK_EQUAL(errorMessage, "Call address error.");

        auto& features = const_cast<bcos::ledger::Features&>(ledgerConfig.features());
        features.set(bcos::ledger::Features::Flag::feature_sharding);
        HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)>
            hostContext2(rollbackableStorage, rollbackableTransientStorage, blockHeader, message,
                origin, "", 0, seq, *precompiledManager, ledgerConfig, *hashImpl, false, 0,
                bcos::task::syncWait);
        syncWait(hostContext2.prepare());
        BOOST_CHECK_NO_THROW(result.emplace(syncWait(hostContext2.execute())));
    }

    BOOST_CHECK_EQUAL(result->status_code, 0);
    bcos::s256 getIntResult = -1;
    abiCodec.abiOut(bcos::bytesConstRef(result->output_data, result->output_size), getIntResult);
    BOOST_CHECK_EQUAL(getIntResult, 0);
}

static bcos::task::Task<void> testNestConstructor(auto* self, bool web3)
{
    auto features = self->ledgerConfig.features();
    features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
    self->ledgerConfig.setFeatures(features);

    auto result1 = co_await self->call("deployWithDeploy()", {}, web3);

    BOOST_TEST(result1.status_code == 0);
    bcos::Address address1{};
    bcos::codec::abi::ContractABICodec abiCodec(*self->hashImpl);
    abiCodec.abiOut(bcos::bytesConstRef(result1.output_data, result1.output_size), address1);
    BOOST_TEST(address1 != bcos::Address{});

    if (web3)
    {
        bcos::ledger::account::EVMAccount account(self->storage, address1, false);
        auto nonce = co_await account.nonce();
        BOOST_REQUIRE(nonce.has_value());
        BOOST_TEST(nonce.value() == "11");
    }

    auto result2 = co_await self->call(address1, "all()", {}, web3);
    std::vector<bcos::Address> addresses;
    bcos::codec::abi::ContractABICodec abiCodec2(*self->hashImpl);
    abiCodec2.abiOut(bcos::bytesConstRef(result2.output_data, result2.output_size), addresses);

    // 检查新建的合约地址不重复
    // Verify the new contract address is unique
    BOOST_TEST(addresses.size() == 10);
    ::ranges::sort(addresses);
    auto last = ::ranges::unique(addresses);
    BOOST_TEST(::ranges::distance(addresses.begin(), last) == 10);

    for (auto& address2 : addresses)
    {
        BOOST_CHECK_NE(address2, bcos::Address{});
        if (web3)
        {
            bcos::ledger::account::EVMAccount account(self->storage, address2, false);
            auto nonce = co_await account.nonce();
            BOOST_REQUIRE(nonce.has_value());
            BOOST_TEST(nonce.value() == "1");
        }

        auto result3 = co_await self->call(address1, "get(address)", {}, web3, address2);
        BOOST_TEST(result3.status_code == 0);

        bcos::codec::abi::ContractABICodec abiCodec3(*self->hashImpl);
        bcos::s256 num;
        abiCodec3.abiOut(bcos::bytesConstRef(result3.output_data, result3.output_size), addresses);
    }
}

BOOST_AUTO_TEST_CASE(nestConstructor)
{
    syncWait([](decltype(this) self) -> Task<void> {
        co_await testNestConstructor(self, true);
        co_await testNestConstructor(self, false);
    }(this));
}

BOOST_AUTO_TEST_CASE(codeSize)
{
    syncWait([this]() -> Task<void> {
        bcostars::protocol::BlockHeaderImpl blockHeader;
        blockHeader.setVersion(static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_3_VERSION));

        int64_t number = 0;
        blockHeader.setNumber(number++);
        blockHeader.calculateHash(*hashImpl);

        evmc_message message{};

        HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)>
            codeSizeHostContext(rollbackableStorage, rollbackableTransientStorage, blockHeader,
                message, {}, "", 0, seq, *precompiledManager, ledgerConfig, *hashImpl, false, 0,
                bcos::task::syncWait);

        auto builtinAddress = bcos::unhexAddress("0000000000000000000000000000000000000001");
        auto size = co_await codeSizeHostContext.codeSizeAt(builtinAddress);
        BOOST_CHECK_EQUAL(size, 0);
    }());
}

BOOST_AUTO_TEST_CASE(transferBalance)
{
    syncWait([this]() -> Task<void> {
        bcostars::protocol::BlockHeaderImpl blockHeader;
        blockHeader.setVersion(static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_3_VERSION));

        static int64_t number = 0;
        blockHeader.setNumber(number++);
        blockHeader.calculateHash(*hashImpl);

        co_await initBFS(blockHeader, *hashImpl);
        blockHeader.setNumber(number++);
        blockHeader.calculateHash(*hashImpl);

        evmc_message message{};
        message.sender = bcos::unhexAddress("0000000000000000000000000000000000000001");
        message.recipient = bcos::unhexAddress("0000000000000000000000000000000000000002");
        message.value = bcos::toEvmC(bcos::u256(1000));
        message.kind = EVMC_CALL;
        message.gas = 21000;

        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> senderAccount(
            rollbackableStorage, message.sender, false);
        co_await senderAccount.setBalance(bcos::u256(1001));
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> recipientAccount(
            rollbackableStorage, message.recipient, false);
        co_await recipientAccount.setBalance(bcos::u256(0));

        evmc_address origin{};
        HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)>
            transferHostContext(rollbackableStorage, rollbackableTransientStorage, blockHeader,
                message, origin, "", 0, seq, *precompiledManager, ledgerConfig, *hashImpl, false, 0,
                bcos::task::syncWait);
        co_await transferHostContext.prepare();
        auto evmResult = co_await transferHostContext.execute();
        BOOST_CHECK_EQUAL(evmResult.status_code, EVMC_SUCCESS);
        BOOST_CHECK_EQUAL(evmResult.gas_left, 0);
        BOOST_CHECK_EQUAL(co_await senderAccount.balance(), bcos::u256(1001));
        BOOST_CHECK_EQUAL(co_await recipientAccount.balance(), bcos::u256(0));

        ledgerConfig.setBalanceTransfer(true);
        transferHostContext.mutableMessage().gas = 21000;
        evmResult = co_await transferHostContext.execute();
        BOOST_CHECK_EQUAL(evmResult.status_code, EVMC_SUCCESS);
        BOOST_CHECK_EQUAL(co_await senderAccount.balance(), bcos::u256(1));
        BOOST_CHECK_EQUAL(co_await recipientAccount.balance(), bcos::u256(1000));
    }());
}

BOOST_AUTO_TEST_CASE(evmTimestamp)
{
    syncWait([](decltype(this) self) -> Task<void> {
        auto features = self->ledgerConfig.features();
        features.setGenesisFeatures(bcos::protocol::BlockVersion::MAX_VERSION);
        features.set(bcos::ledger::Features::Flag::feature_evm_timestamp);
        self->ledgerConfig.setFeatures(features);
        self->blockHeader.setTimestamp(100 * 1001);

        auto result = co_await self->call("timestamp()", {}, true);

        BOOST_TEST(result.status_code == 0);
        bcos::u256 getIntResult = -1;
        bcos::codec::abi::ContractABICodec abiCodec(*bcos::executor::GlobalHashImpl::g_hashImpl);
        abiCodec.abiOut(bcos::bytesConstRef(result.output_data, result.output_size), getIntResult);
        BOOST_TEST(getIntResult == 100);
    }(this));
}

BOOST_AUTO_TEST_SUITE_END()