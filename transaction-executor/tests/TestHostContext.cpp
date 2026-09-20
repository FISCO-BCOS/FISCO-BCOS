#include "../bcos-transaction-executor/precompiled/PrecompiledManager.h"
#include "../bcos-transaction-executor/vm/HostContext.h"
#include "TestBytecode.h"
#include "TestMemoryStorage.h"
#include "bcos-codec/bcos-codec/abi/ContractABICodec.h"
#include "bcos-crypto/interfaces/crypto/CryptoSuite.h"
#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/AccountTableName.h"
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
#include <bcos-framework/testutils/ScopedNodeAddressTableMode.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <evmc/evmc.h>
#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>
#include <algorithm>
#include <atomic>
#include <iterator>
#include <memory>
#include <range/v3/algorithm/sort.hpp>
#include <range/v3/algorithm/unique.hpp>
#include <bcos-utilities/BoostLog.h>

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
            .sender = {},
            .input_data = (const uint8_t*)helloworldBytecodeBinary.data(),
            .input_size = helloworldBytecodeBinary.size(),
            .value = {},
            .create2_salt = {},
            .code_address = {},
            .code = nullptr,
            .code_size = 0};
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

        // 5M gas keeps nested-create tests (e.g. nestConstructor's 10x CREATE chain)
        // within budget under EIP-2200 correct SSTORE pricing (ADDED=20000, not 5000).
        evmc_message message = {.kind = EVMC_CALL,
            .flags = 0,
            .depth = 0,
            .gas = 5000000,
            .recipient = address,
            .sender = sender,
            .input_data = input.data(),
            .input_size = input.size(),
            .value = {},
            .create2_salt = {},
            .code_address = address,
            .code = nullptr,
            .code_size = 0};
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
            .sender = {},
            .input_data = input.data(),
            .input_size = input.size(),
            .value = {},
            .create2_salt = {},
            .code_address = callAddress,
            .code = nullptr,
            .code_size = 0};
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
            .sender = {},
            .input_data = nullptr,
            .input_size = 0,
            .value = {},  // zero value
            .create2_salt = {},
            .code_address = {},
            .code = nullptr,
            .code_size = 0};

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
            .sender = {},
            .input_data = input.data(),
            .input_size = input.size(),
            .value = {},
            .create2_salt = {},
            .code_address = callAddress,
            .code = nullptr,
            .code_size = 0};
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
        bcos::ledger::account::EVMAccount account(self->storage, address1, bcos::ledger::account::AddressTableMode::Hex);
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
            bcos::ledger::account::EVMAccount account(self->storage, address2, bcos::ledger::account::AddressTableMode::Hex);
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
            rollbackableStorage, message.sender, bcos::ledger::account::AddressTableMode::Hex);
        co_await senderAccount.setBalance(bcos::u256(1001));
        bcos::ledger::account::EVMAccount<decltype(rollbackableStorage)> recipientAccount(
            rollbackableStorage, message.recipient, bcos::ledger::account::AddressTableMode::Hex);
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

BOOST_AUTO_TEST_CASE(setStorageStatusWithBugfix)
{
    // FIB-94: When bugfix_evm_storage_status is ON, setStorage must return the
    // correct 4-state evmc_storage_status for each transition.
    syncWait([this]() -> Task<void> {
        blockHeader.setNumber(seq++);
        blockHeader.calculateHash(*hashImpl);

        bcos::ledger::Features features;
        features.set(bcos::ledger::Features::Flag::bugfix_evm_storage_status);
        ledgerConfig.setFeatures(features);

        evmc_message message = {.kind = EVMC_CALL,
            .flags = 0,
            .depth = 0,
            .gas = 1000000,
            .recipient = helloworldAddress,
            .sender = {},
            .input_data = nullptr,
            .input_size = 0,
            .value = {},
            .create2_salt = {},
            .code_address = helloworldAddress,
            .code = nullptr,
            .code_size = 0};
        evmc_address origin = {};

        HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)>
            hostContext(rollbackableStorage, rollbackableTransientStorage, blockHeader, message,
                origin, "", 0, seq, *precompiledManager, ledgerConfig, *hashImpl, false, 0,
                bcos::task::syncWait);
        co_await hostContext.prepare();

        auto* iface = hostContext.hostInterface();
        auto* hostCtx = hostContext.hostCtx();

        evmc_bytes32 storageKey{};
        storageKey.bytes[31] = 0x42;
        evmc_bytes32 nonZeroValue{};
        nonZeroValue.bytes[31] = 0x01;
        evmc_bytes32 anotherNonZeroValue{};
        anotherNonZeroValue.bytes[31] = 0x02;
        evmc_bytes32 zeroValue{};

        auto status1 = iface->set_storage(hostCtx, &helloworldAddress, &storageKey, &nonZeroValue);
        BOOST_CHECK_EQUAL(status1, EVMC_STORAGE_ADDED);

        auto status2 =
            iface->set_storage(hostCtx, &helloworldAddress, &storageKey, &anotherNonZeroValue);
        BOOST_CHECK_EQUAL(status2, EVMC_STORAGE_MODIFIED);

        auto status3 = iface->set_storage(hostCtx, &helloworldAddress, &storageKey, &zeroValue);
        BOOST_CHECK_EQUAL(status3, EVMC_STORAGE_DELETED);

        auto status4 = iface->set_storage(hostCtx, &helloworldAddress, &storageKey, &zeroValue);
        BOOST_CHECK_EQUAL(status4, EVMC_STORAGE_ASSIGNED);

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(setStorageStatusLegacy)
{
    // FIB-94: Without the bugfix flag, preserve the legacy 2-state return so
    // that existing chains do not fork on the fix.
    syncWait([this]() -> Task<void> {
        blockHeader.setNumber(seq++);
        blockHeader.calculateHash(*hashImpl);

        ledgerConfig.setFeatures(bcos::ledger::Features{});

        evmc_message message = {.kind = EVMC_CALL,
            .flags = 0,
            .depth = 0,
            .gas = 1000000,
            .recipient = helloworldAddress,
            .sender = {},
            .input_data = nullptr,
            .input_size = 0,
            .value = {},
            .create2_salt = {},
            .code_address = helloworldAddress,
            .code = nullptr,
            .code_size = 0};
        evmc_address origin = {};

        evmc_bytes32 storageKey{};
        storageKey.bytes[31] = 0x73;  // distinct key to avoid clashing with the other test
        evmc_bytes32 nonZeroValue{};
        nonZeroValue.bytes[31] = 0x01;
        evmc_bytes32 anotherNonZeroValue{};
        anotherNonZeroValue.bytes[31] = 0x02;
        evmc_bytes32 zeroValue{};

        HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)>
            hostContext(rollbackableStorage, rollbackableTransientStorage, blockHeader, message,
                origin, "", 0, seq, *precompiledManager, ledgerConfig, *hashImpl, false, 0,
                bcos::task::syncWait);
        co_await hostContext.prepare();

        auto* iface = hostContext.hostInterface();
        auto* hostCtx = hostContext.hostCtx();

        auto status1 = iface->set_storage(hostCtx, &helloworldAddress, &storageKey, &nonZeroValue);
        BOOST_CHECK_EQUAL(status1, EVMC_STORAGE_MODIFIED);  // buggy: should be ADDED

        auto status2 =
            iface->set_storage(hostCtx, &helloworldAddress, &storageKey, &anotherNonZeroValue);
        BOOST_CHECK_EQUAL(status2, EVMC_STORAGE_MODIFIED);

        auto status3 = iface->set_storage(hostCtx, &helloworldAddress, &storageKey, &zeroValue);
        BOOST_CHECK_EQUAL(status3, EVMC_STORAGE_DELETED);

        auto status4 = iface->set_storage(hostCtx, &helloworldAddress, &storageKey, &zeroValue);
        BOOST_CHECK_EQUAL(status4, EVMC_STORAGE_DELETED);  // buggy: should be ASSIGNED

        co_return;
    }());
}

BOOST_AUTO_TEST_CASE(nodeAddressTableModeSingleton)
{
    namespace account = bcos::ledger::account;
    // Default without the startup flow: Hex — the pre-detection behavior every library and
    // test inherits.
    BOOST_CHECK(account::nodeAddressTableMode() == account::AddressTableMode::Hex);

    // Set/get round trip through both modes. The singleton is process-global, so the guard
    // restores the Hex default on the way out (the startup flow sets it exactly once,
    // single-threaded).
    bcos::test::ScopedNodeAddressTableMode const modeGuard(account::AddressTableMode::Hex);
    for (auto mode : {account::AddressTableMode::Hex, account::AddressTableMode::Binary})
    {
        account::setNodeAddressTableMode(mode);
        BOOST_CHECK(account::nodeAddressTableMode() == mode);
    }
}

// The two layouts are disjoint namespaces: there is no runtime mixed mode, so a Binary-mode
// account sees nothing of the hex table's rows and vice versa. A chain changes encoding only
// through the one-shot boot-time migration ([storage] migrate_account_tables_to_binary),
// which rewrites the physical keys — never through a per-read fallback.
BOOST_AUTO_TEST_CASE(binaryAndHexLayoutsAreDisjoint)
{
    syncWait([this]() -> Task<void> {
        namespace account = bcos::ledger::account;
        auto address = bcos::unhexAddress("0x4200000000000000000000000000000000001234");
        std::string const hexTable = "/apps/4200000000000000000000000000000000001234";

        evmc_bytes32 slotKey{};
        slotKey.bytes[31] = 0x11;
        evmc_bytes32 slotValue{};
        slotValue.bytes[31] = 0x42;

        bcos::bytes code{0x60, 0x00, 0x60, 0x00, 0xf3};
        auto const codeHash = hashImpl->hash(code);

        // Old-layout rows: written through the Hex-mode account, exactly as an unmigrated
        // node's blocks would have written them.
        account::EVMAccount hexAccount(storage, address, account::AddressTableMode::Hex);
        BOOST_CHECK_EQUAL(hexAccount.address(), hexTable);
        co_await hexAccount.create();
        co_await hexAccount.setNonce("7");
        co_await hexAccount.setBalance(bcos::u256(12345));
        co_await hexAccount.setCode(code, "the-abi", codeHash);
        co_await hexAccount.setStorage(slotKey, slotValue);

        // (a) Plain Binary mode sees nothing of the hex-table data.
        account::EVMAccount binaryAccount(storage, address, account::AddressTableMode::Binary);
        BOOST_CHECK(binaryAccount.address() != hexTable);
        BOOST_CHECK(!(co_await binaryAccount.exists()));
        BOOST_CHECK(!(co_await binaryAccount.nonce()).has_value());
        BOOST_CHECK_EQUAL(co_await binaryAccount.balance(), bcos::u256(0));
        BOOST_CHECK_EQUAL(co_await binaryAccount.codeHash(), bcos::h256{});
        BOOST_CHECK(!(co_await binaryAccount.code()).has_value());
        evmc_bytes32 const zeroValue{};
        auto zeroSlot = co_await binaryAccount.storage(slotKey);
        BOOST_CHECK(std::equal(std::begin(zeroSlot.bytes), std::end(zeroSlot.bytes),
            std::begin(zeroValue.bytes), std::end(zeroValue.bytes)));

        // (b) And the other way around: a fresh binary table is invisible in Hex mode.
        co_await binaryAccount.create();
        co_await binaryAccount.setNonce("8");
        evmc_bytes32 newSlotKey{};
        newSlotKey.bytes[31] = 0x22;
        evmc_bytes32 newSlotValue{};
        newSlotValue.bytes[31] = 0x77;
        co_await binaryAccount.setStorage(newSlotKey, newSlotValue);

        BOOST_CHECK(co_await binaryAccount.nonce() == std::optional<std::string>{"8"});
        auto writtenSlot = co_await binaryAccount.storage(newSlotKey);
        BOOST_CHECK(std::equal(std::begin(writtenSlot.bytes), std::end(writtenSlot.bytes),
            std::begin(newSlotValue.bytes), std::end(newSlotValue.bytes)));
        // The hex table keeps its old rows, untouched by the binary writes (no write-time
        // twin removal exists anymore).
        BOOST_CHECK(co_await hexAccount.nonce() == std::optional<std::string>{"7"});
        auto untouchedSlot = co_await hexAccount.storage(newSlotKey);
        BOOST_CHECK(std::equal(std::begin(untouchedSlot.bytes), std::end(untouchedSlot.bytes),
            std::begin(zeroValue.bytes), std::end(zeroValue.bytes)));

        co_return;
    }());
}

// Account table-name coding helpers (ledger/AccountTableName.h): the two physical
// encodings of one logical account table and the canonical form used for hashing.
BOOST_AUTO_TEST_CASE(accountTableNameCoding)
{
    namespace account = bcos::ledger::account;
    std::string const hexTable = "/apps/4200000000000000000000000000000000001234";
    std::string const binTable = [&] {
        auto address = bcos::unhexAddress("0x4200000000000000000000000000000000001234");
        std::string name(account::BINARY_TABLE_PREFIX);
        name.append(reinterpret_cast<const char*>(address.bytes), sizeof(address.bytes));  // NOLINT
        return name;
    }();
    BOOST_REQUIRE_EQUAL(binTable.size(), 23u);

    // Probes: prefix+shape disambiguate the two encodings.
    BOOST_CHECK(account::isHexAccountTableName(hexTable));
    BOOST_CHECK(!account::isBinaryAccountTableName(hexTable));
    BOOST_CHECK(account::isBinaryAccountTableName(binTable));
    BOOST_CHECK(!account::isHexAccountTableName(binTable));

    // F1 regression pin: a "/apps/" table with a 20-char name (mkdir/link/CNS can produce
    // these) is a plain BFS table, NOT a binary account table — the binary layout lives
    // under the reserved "/s/" namespace, so classification is never by length alone.
    std::string const twentyCharAppsName = "/apps/" + std::string(20, 'x');
    BOOST_CHECK(!account::isBinaryAccountTableName(twentyCharAppsName));
    BOOST_CHECK(!account::isHexAccountTableName(twentyCharAppsName));
    BOOST_CHECK(account::binaryToHexAccountTableName(twentyCharAppsName).empty());
    BOOST_CHECK_EQUAL(account::canonicalTableNameForHash(twentyCharAppsName), twentyCharAppsName);

    // Negative probes: wrong prefix, wrong length, uppercase hex, non-hex char.
    BOOST_CHECK(!account::isHexAccountTableName("/sys/4200000000000000000000000000000000001234"));
    BOOST_CHECK(!account::isHexAccountTableName("/apps/42000000000000000000000000000000000012"));  // 38
    BOOST_CHECK(!account::isHexAccountTableName("/apps/4200000000000000000000000000000000001234ff"));  // 42
    BOOST_CHECK(!account::isHexAccountTableName("/apps/420000000000000000000000000000000000ABCD"));
    BOOST_CHECK(!account::isHexAccountTableName("/apps/zz00000000000000000000000000000000001234"));
    BOOST_CHECK(!account::isBinaryAccountTableName(binTable.substr(0, 22)));  // 19 address bytes
    BOOST_CHECK(!account::isBinaryAccountTableName(binTable + "x"));          // 21 address bytes
    BOOST_CHECK(!account::isBinaryAccountTableName("/s/"));
    BOOST_CHECK(!account::isBinaryAccountTableName("/apps/"));
    // An "_accessAuth" auth table is neither encoding — it is out of scope (not migrated,
    // not normalized).
    BOOST_CHECK(!account::isHexAccountTableName(hexTable + "_accessAuth"));
    BOOST_CHECK(!account::isBinaryAccountTableName(binTable + "_accessAuth"));

    // Round trip, lowercase canonical hex.
    BOOST_CHECK_EQUAL(account::binaryToHexAccountTableName(binTable), hexTable);
    BOOST_CHECK(account::hexToBinaryAccountTableName(hexTable) == binTable);

    // Invalid input → empty string (documented caller error).
    BOOST_CHECK(account::binaryToHexAccountTableName(hexTable).empty());
    BOOST_CHECK(account::binaryToHexAccountTableName("/s/short").empty());
    BOOST_CHECK(account::hexToBinaryAccountTableName(binTable).empty());

    // Canonicalization: binary → hex, hex identical, everything else untouched.
    BOOST_CHECK_EQUAL(account::canonicalTableNameForHash(binTable), hexTable);
    BOOST_CHECK_EQUAL(account::canonicalTableNameForHash(hexTable), hexTable);
    BOOST_CHECK_EQUAL(account::canonicalTableNameForHash("s_tables"), "s_tables");
    BOOST_CHECK_EQUAL(account::canonicalTableNameForHash("/sys/status"), "/sys/status");
    BOOST_CHECK_EQUAL(account::canonicalTableNameForHash("/apps/someContract"),
        "/apps/someContract");
    BOOST_CHECK_EQUAL(
        account::canonicalTableNameForHash(hexTable + "_accessAuth"), hexTable + "_accessAuth");
}

// Write-side isolation: with the runtime fallback mode gone, a Binary-mode write touches
// only the binary table and leaves any hex twin row alone (and symmetrically for Hex mode).
// Physical consolidation is the migration tool's job, not the write path's.
BOOST_AUTO_TEST_CASE(binaryWritesLeaveHexRowsUntouched)
{
    syncWait([this]() -> Task<void> {
        namespace account = bcos::ledger::account;
        auto address = bcos::unhexAddress("0x4200000000000000000000000000000000005678");
        std::string const hexTable = "/apps/4200000000000000000000000000000000005678";
        std::string const binTable = account::hexToBinaryAccountTableName(hexTable);
        BOOST_REQUIRE(!binTable.empty());

        evmc_bytes32 slotKey{};
        slotKey.bytes[31] = 0x33;
        evmc_bytes32 slotValue{};
        slotValue.bytes[31] = 0x55;
        evmc_bytes32 newSlotValue{};
        newSlotValue.bytes[31] = 0x66;
        bcos::bytes code{0x60, 0x00, 0x60, 0x00, 0xf3};
        auto const codeHash = hashImpl->hash(code);

        // Rows in the legacy hex layout.
        account::EVMAccount hexAccount(storage, address, account::AddressTableMode::Hex);
        co_await hexAccount.create();
        co_await hexAccount.setNonce("3");
        co_await hexAccount.setBalance(bcos::u256(999));
        co_await hexAccount.setStorage(slotKey, slotValue);

        // (a) Binary-mode writes land in the binary table only; the hex twins stay put.
        account::EVMAccount binaryAccount(storage, address, account::AddressTableMode::Binary);
        co_await binaryAccount.create();
        co_await binaryAccount.setNonce("4");
        co_await binaryAccount.setBalance(bcos::u256(1000));
        co_await binaryAccount.setStorage(slotKey, newSlotValue);
        co_await binaryAccount.setCode(code, "the-abi", codeHash);

        // Both registrations and both row sets now exist side by side...
        BOOST_CHECK(co_await storage2::existsOne(storage, StateKeyView{bcos::ledger::SYS_TABLES, hexTable}));
        BOOST_CHECK(co_await storage2::existsOne(storage, StateKeyView{bcos::ledger::SYS_TABLES, binTable}));
        BOOST_CHECK(co_await hexAccount.nonce() == std::optional<std::string>{"3"});
        BOOST_CHECK_EQUAL(co_await hexAccount.balance(), bcos::u256(999));
        BOOST_CHECK(co_await binaryAccount.nonce() == std::optional<std::string>{"4"});
        BOOST_CHECK_EQUAL(co_await binaryAccount.balance(), bcos::u256(1000));
        BOOST_CHECK_EQUAL(co_await binaryAccount.codeHash(), codeHash);
        auto binSlot = co_await binaryAccount.storage(slotKey);
        BOOST_CHECK(std::equal(std::begin(binSlot.bytes), std::end(binSlot.bytes),
            std::begin(newSlotValue.bytes), std::end(newSlotValue.bytes)));
        auto hexSlot = co_await hexAccount.storage(slotKey);
        BOOST_CHECK(std::equal(std::begin(hexSlot.bytes), std::end(hexSlot.bytes),
            std::begin(slotValue.bytes), std::end(slotValue.bytes)));

        // (b) And Hex mode is symmetric: its writes touch only the hex table.
        co_await hexAccount.setNonce("5");
        BOOST_CHECK(co_await hexAccount.nonce() == std::optional<std::string>{"5"});
        BOOST_CHECK(co_await binaryAccount.nonce() == std::optional<std::string>{"4"});

        co_return;
    }());
}

// FIB-82 follow-up: on a Binary-mode node the create flow must create the auth table at the
// HEX path ("/apps/<40 hex>_accessAuth") even with bugfix_auth_check OFF (the default
// fixture ledgerConfig sets no bugfix flags) — ContractAuthMgrPrecompiled looks up auth
// tables by hex path unconditionally, and auth table names are not normalized in
// Entry::hash, so a binary auth path would both break the precompiled's lookup and fork
// the XOR root against Hex nodes. The process-global mode is restored to Hex on the way
// out by the scoped guard (the startup flow sets it exactly once, single-threaded).
BOOST_AUTO_TEST_CASE(createOnBinaryNodeWritesHexAuthTable)
{
    namespace account = bcos::ledger::account;
    bcos::test::ScopedNodeAddressTableMode const modeGuard(account::AddressTableMode::Binary);
    syncWait([this]() -> Task<void> {
        auto codeAddress = bcos::unhexAddress("0x4200000000000000000000000000000000004321");
        std::string const hexAuthTable = "/apps/4200000000000000000000000000000000004321_accessAuth";

        // blockHeader.number() != 0 so executeCreate runs createAuthTable (the fixture's own
        // deploy runs at number 0 and skips it).
        bcostars::protocol::BlockHeaderImpl createBlockHeader;
        createBlockHeader.setVersion(
            static_cast<uint32_t>(bcos::protocol::BlockVersion::MAX_VERSION));
        createBlockHeader.setNumber(1);
        createBlockHeader.calculateHash(*hashImpl);

        std::string helloworldBytecodeBinary;
        boost::algorithm::unhex(helloworldBytecode, std::back_inserter(helloworldBytecodeBinary));
        evmc_message message = {.kind = EVMC_CREATE,
            .flags = 0,
            .depth = 0,
            .gas = 300 * 10000,
            .recipient = {},
            .sender = {},
            .input_data = (const uint8_t*)helloworldBytecodeBinary.data(),
            .input_size = helloworldBytecodeBinary.size(),
            .value = {},
            .create2_salt = {},
            .code_address = codeAddress,
            .code = nullptr,
            .code_size = 0};
        evmc_address origin = {};

        HostContext<decltype(rollbackableStorage), decltype(rollbackableTransientStorage)>
            hostContext(rollbackableStorage, rollbackableTransientStorage, createBlockHeader,
                message, origin, "", 0, seq, *precompiledManager, ledgerConfig, *hashImpl, false,
                0, bcos::task::syncWait);
        co_await hostContext.prepare();
        auto result = co_await hostContext.execute();
        BOOST_REQUIRE_EQUAL(result.status_code, 0);

        // The auth table must exist at the hex path...
        BOOST_CHECK(co_await storage2::existsOne(
            storage, StateKeyView{bcos::ledger::SYS_TABLES, hexAuthTable}));
        // ...and NOT at the binary path that the recipient account's path() would have
        // produced before the fix.
        auto const binAuthTable =
            account::hexToBinaryAccountTableName(
                "/apps/4200000000000000000000000000000000004321") +
            "_accessAuth";
        BOOST_REQUIRE(binAuthTable.size() > std::string_view{"_accessAuth"}.size());
        BOOST_CHECK(!co_await storage2::existsOne(
            storage, StateKeyView{bcos::ledger::SYS_TABLES, binAuthTable}));
        co_return;
    }());
}

BOOST_AUTO_TEST_SUITE_END()
