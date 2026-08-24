#include "../bcos-transaction-executor/TransactionExecutorImpl.h"
#include "../tests/TestBytecode.h"
#include "../tests/TestMemoryStorage.h"
#include "BenchmarkERC20.h"
#include "bcos-codec/bcos-codec/abi/ContractABICodec.h"
#include "bcos-crypto/interfaces/crypto/CryptoSuite.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <benchmark/benchmark.h>
#include <boost/throw_exception.hpp>
#include <stdexcept>

using namespace bcos;
using namespace bcos::storage2::memory_storage;
using namespace bcos::executor_v1;

using ReceiptFactory = bcostars::protocol::TransactionReceiptFactoryImpl;

struct Fixture
{
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    MutableStorage m_backendStorage;
    ReceiptFactory m_receiptFactory;
    PrecompiledManager m_precompiledManager;
    bcos::executor_v1::TransactionExecutorImpl m_executor;
    bcos::bytes m_helloworldBytecodeBinary;

    bcostars::BlockHeader tarsBlockHeader;
    bcostars::protocol::BlockHeaderImpl blockHeader;
    ledger::LedgerConfig ledgerConfig;

    Fixture()
      : m_cryptoSuite(std::make_shared<bcos::crypto::CryptoSuite>(
            std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr)),
        m_receiptFactory(m_cryptoSuite),
        m_precompiledManager(std::make_shared<bcos::crypto::Keccak256>()),
        m_executor(
            m_receiptFactory, std::make_shared<bcos::crypto::Keccak256>(), m_precompiledManager),
        blockHeader()
    {
        boost::log::core::get()->set_logging_enabled(false);
        bcos::executor::GlobalHashImpl::g_hashImpl = std::make_shared<bcos::crypto::Keccak256>();
        boost::algorithm::unhex(helloworldBytecode, std::back_inserter(m_helloworldBytecodeBinary));
        blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION);
    }

    std::string deployContract()
    {
        std::string contractAddress;
        task::syncWait([this, &contractAddress]() -> task::Task<void> {
            bcostars::protocol::TransactionImpl createTransaction(
                [inner = bcostars::Transaction()]() mutable { return std::addressof(inner); });
            createTransaction.mutableInner().data.input.assign(
                m_helloworldBytecodeBinary.begin(), m_helloworldBytecodeBinary.end());
            auto receipt = co_await m_executor.executeTransaction(
                m_backendStorage, blockHeader, createTransaction, 0, ledgerConfig, false);
            contractAddress = receipt->contractAddress();
        }());

        return contractAddress;
    }
};

static void create(benchmark::State& state)
{
    Fixture fixture;

    bcostars::protocol::TransactionImpl transaction(
        [inner = bcostars::Transaction()]() mutable { return std::addressof(inner); });
    transaction.mutableInner().data.input.assign(
        fixture.m_helloworldBytecodeBinary.begin(), fixture.m_helloworldBytecodeBinary.end());
    transaction.mutableInner().dataHash.resize(1);

    task::syncWait(
        [&](benchmark::State& state, decltype(transaction)& transaction) -> task::Task<void> {
            int contextID = 0;
            for (auto const& it : state)
            {
                ++contextID;
                [[maybe_unused]] auto receipt =
                    co_await fixture.m_executor.executeTransaction(fixture.m_backendStorage,
                        fixture.blockHeader, transaction, contextID, fixture.ledgerConfig, false);
            }
        }(state, transaction));
}

static void call_setInt(benchmark::State& state)
{
    Fixture fixture;
    std::string contractAddress = fixture.deployContract();

    bcostars::protocol::TransactionImpl transaction;

    bcos::codec::abi::ContractABICodec abiCodec(*bcos::executor::GlobalHashImpl::g_hashImpl);

    task::syncWait([&](benchmark::State& state) -> task::Task<void> {
        int contextID = 0;
        for (auto const& it : state)
        {
            auto input = abiCodec.abiIn("setInt(int256)", bcos::s256(contextID));
            transaction.mutableInner().data.input.assign(input.begin(), input.end());
            transaction.mutableInner().data.to = contractAddress;
            transaction.mutableInner().dataHash.resize(1);

            ++contextID;
            [[maybe_unused]] auto receipt =
                co_await fixture.m_executor.executeTransaction(fixture.m_backendStorage,
                    fixture.blockHeader, transaction, contextID, fixture.ledgerConfig, false);
        }
    }(state));
}

static void call_setString(benchmark::State& state)
{
    Fixture fixture;
    std::string contractAddress = fixture.deployContract();

    bcostars::protocol::TransactionImpl transaction;

    bcos::codec::abi::ContractABICodec abiCodec(*bcos::executor::GlobalHashImpl::g_hashImpl);

    task::syncWait([&](benchmark::State& state) -> task::Task<void> {
        int contextID = 0;
        for (auto const& it : state)
        {
            auto input = abiCodec.abiIn(
                "setString(string)", fmt::format("Hello world, fisco-bcos! {}", contextID));
            transaction.mutableInner().data.input.assign(input.begin(), input.end());
            transaction.mutableInner().data.to = contractAddress;
            transaction.mutableInner().dataHash.resize(1);
            ++contextID;
            [[maybe_unused]] auto receipt =
                co_await fixture.m_executor.executeTransaction(fixture.m_backendStorage,
                    fixture.blockHeader, transaction, contextID, fixture.ledgerConfig, false);
        }
    }(state));
}

static void call_delegateCall(benchmark::State& state)
{
    Fixture fixture;
    std::string contractAddress = fixture.deployContract();

    bcos::codec::abi::ContractABICodec abiCodec(*bcos::executor::GlobalHashImpl::g_hashImpl);
    bcostars::protocol::TransactionImpl transaction1(
        [inner = bcostars::Transaction()]() mutable { return std::addressof(inner); });
    auto input = abiCodec.abiIn("delegateCall()");
    transaction1.mutableInner().data.input.assign(input.begin(), input.end());
    transaction1.mutableInner().data.to = contractAddress;
    transaction1.mutableInner().dataHash.resize(1);

    task::syncWait([&](benchmark::State& state) -> task::Task<void> {
        int contextID = 0;
        for (auto const& it : state)
        {
            ++contextID;
            [[maybe_unused]] auto receipt =
                co_await fixture.m_executor.executeTransaction(fixture.m_backendStorage,
                    fixture.blockHeader, transaction1, contextID, fixture.ledgerConfig, false);
        }
    }(state));
}

static void call_deployAndCall(benchmark::State& state)
{
    Fixture fixture;
    std::string contractAddress = fixture.deployContract();

    bcos::codec::abi::ContractABICodec abiCodec(*bcos::executor::GlobalHashImpl::g_hashImpl);
    bcostars::protocol::TransactionImpl transaction1(
        [inner = bcostars::Transaction()]() mutable { return std::addressof(inner); });
    auto input = abiCodec.abiIn("deployAndCall(int256)", bcos::s256(999));
    transaction1.mutableInner().data.input.assign(input.begin(), input.end());
    transaction1.mutableInner().data.to = contractAddress;
    transaction1.mutableInner().dataHash.resize(1);

    task::syncWait([&](benchmark::State& state) -> task::Task<void> {
        int contextID = 0;
        for (auto const& it : state)
        {
            ++contextID;
            [[maybe_unused]] auto receipt =
                co_await fixture.m_executor.executeTransaction(fixture.m_backendStorage,
                    fixture.blockHeader, transaction1, contextID, fixture.ledgerConfig, false);
        }
    }(state));
}

BENCHMARK(create);
BENCHMARK(call_setInt);
BENCHMARK(call_setString);
BENCHMARK(call_delegateCall);
BENCHMARK(call_deployAndCall);

// ---------------------------------------------------------------------------
// Standard ERC-20 workload (shared with ethereum-executor/benchmark, which
// runs the identical bytecode, account set and transaction stream against the
// pure-Ethereum executor for a side-by-side comparison).
// ---------------------------------------------------------------------------
struct ERC20Fixture
{
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    MutableStorage m_backendStorage;
    ReceiptFactory m_receiptFactory;
    PrecompiledManager m_precompiledManager;
    bcos::executor_v1::TransactionExecutorImpl m_executor;
    bcos::bytes m_erc20BytecodeBinary;

    bcostars::BlockHeader tarsBlockHeader;
    bcostars::protocol::BlockHeaderImpl blockHeader;
    ledger::LedgerConfig ledgerConfig;
    bcos::codec::abi::ContractABICodec m_abiCodec;

    const bcos::Address m_deployer = benchmark_erc20::userAddress(benchmark_erc20::DEPLOYER_INDEX);
    const bcos::Address m_spender = benchmark_erc20::userAddress(benchmark_erc20::SPENDER_INDEX);
    const bcos::Address m_receiver = benchmark_erc20::userAddress(benchmark_erc20::RECEIVER_INDEX);

    std::string m_contractAddress;
    uint64_t m_deployerNonce = 0;

    ERC20Fixture()
      : m_cryptoSuite(std::make_shared<bcos::crypto::CryptoSuite>(
            std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr)),
        m_receiptFactory(m_cryptoSuite),
        m_precompiledManager(std::make_shared<bcos::crypto::Keccak256>()),
        m_executor(
            m_receiptFactory, std::make_shared<bcos::crypto::Keccak256>(), m_precompiledManager),
        blockHeader(),
        m_abiCodec(*m_cryptoSuite->hashImpl())
    {
        boost::log::core::get()->set_logging_enabled(false);
        bcos::executor::GlobalHashImpl::g_hashImpl = std::make_shared<bcos::crypto::Keccak256>();
        boost::algorithm::unhex(
            benchmark_erc20::erc20Bytecode, std::back_inserter(m_erc20BytecodeBinary));
        blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION);
    }

    // Execute one transaction during setup (unmeasured) and require success.
    task::Task<void> setupTransaction(bcostars::protocol::TransactionImpl& transaction)
    {
        auto receipt = co_await m_executor.executeTransaction(
            m_backendStorage, blockHeader, transaction, 0, ledgerConfig, false);
        if (receipt->status() != 0)
        {
            BOOST_THROW_EXCEPTION(std::runtime_error(fmt::format(
                "ERC-20 setup transaction failed, status: {}, {}", receipt->status(),
                receipt->message())));
        }
    }

    // Deploy the token; the deployer funds every user with
    // USER_INITIAL_BALANCE so transferFrom has balances to move.
    // Every setup transaction runs in its own syncWait: bcos-task only
    // tail-calls symmetric-transfer coroutine chains under optimization, so a
    // single coroutine awaiting 1000+ transactions in a row overflows the
    // stack in unoptimized (Debug/ASAN) builds.
    void deploy()
    {
        auto transaction = benchmark_erc20::makeTransaction(
            bcos::ref(m_erc20BytecodeBinary), "", m_deployer, m_deployerNonce++);
        task::syncWait([this, &transaction]() -> task::Task<void> {
            auto receipt = co_await m_executor.executeTransaction(
                m_backendStorage, blockHeader, transaction, 0, ledgerConfig, false);
            if (receipt->status() != 0)
            {
                BOOST_THROW_EXCEPTION(std::runtime_error(fmt::format(
                    "ERC-20 deploy failed, status: {}, {}", receipt->status(),
                    receipt->message())));
            }
            m_contractAddress = receipt->contractAddress();
        }());

        for (uint64_t i = 1; i <= benchmark_erc20::USER_COUNT; ++i)
        {
            auto input = m_abiCodec.abiIn("transfer(address,uint256)",
                benchmark_erc20::userAddress(i),
                bcos::u256(benchmark_erc20::USER_INITIAL_BALANCE));
            auto fundTransaction = benchmark_erc20::makeTransaction(
                bcos::ref(input), m_contractAddress, m_deployer, m_deployerNonce++);
            task::syncWait(setupTransaction(fundTransaction));
        }
    }

    // Every user approves the spender (user nonce 0 — each user's first
    // transaction), so the transferFrom benchmark can run immediately.
    void approveAll()
    {
        for (uint64_t i = 1; i <= benchmark_erc20::USER_COUNT; ++i)
        {
            auto input = m_abiCodec.abiIn("approve(address,uint256)", m_spender,
                bcos::u256("1000000000000000000"));  // 1e18
            auto transaction = benchmark_erc20::makeTransaction(
                bcos::ref(input), m_contractAddress, benchmark_erc20::userAddress(i), 0);
            task::syncWait(setupTransaction(transaction));
        }
    }
};

// USER_COUNT users repeatedly approve the spender; iteration i uses user
// (i % USER_COUNT) with per-sender nonce (i / USER_COUNT).
static void erc20_approve(benchmark::State& state)
{
    ERC20Fixture fixture;
    fixture.deploy();

    auto input = fixture.m_abiCodec.abiIn(
        "approve(address,uint256)", fixture.m_spender, bcos::u256("1000000000000000000"));

    task::syncWait([&](benchmark::State& state) -> task::Task<void> {
        uint64_t iteration = 0;
        for (auto const& it : state)
        {
            auto transaction = benchmark_erc20::makeTransaction(bcos::ref(input),
                fixture.m_contractAddress,
                benchmark_erc20::userAddress(iteration % benchmark_erc20::USER_COUNT + 1),
                iteration / benchmark_erc20::USER_COUNT);
            auto receipt = co_await fixture.m_executor.executeTransaction(fixture.m_backendStorage,
                fixture.blockHeader, transaction, 0, fixture.ledgerConfig, false);
            if (receipt->status() != 0)
            {
                state.SkipWithError(
                    fmt::format("approve failed, status: {}", receipt->status()).c_str());
                break;
            }
            ++iteration;
        }
    }(state));
}

// The spender pulls 1 wei from user (i % USER_COUNT) to the receiver;
// per-sender (spender) nonce is i. Inputs are precomputed per user so the
// measured loop only rebuilds the transaction envelope.
static void erc20_transferFrom(benchmark::State& state)
{
    ERC20Fixture fixture;
    fixture.deploy();
    fixture.approveAll();

    std::vector<bcos::bytes> inputs;
    inputs.reserve(benchmark_erc20::USER_COUNT);
    for (uint64_t i = 1; i <= benchmark_erc20::USER_COUNT; ++i)
    {
        inputs.push_back(fixture.m_abiCodec.abiIn("transferFrom(address,address,uint256)",
            benchmark_erc20::userAddress(i), fixture.m_receiver,
            bcos::u256(benchmark_erc20::TRANSFER_FROM_AMOUNT)));
    }

    task::syncWait([&](benchmark::State& state) -> task::Task<void> {
        uint64_t iteration = 0;
        for (auto const& it : state)
        {
            auto transaction = benchmark_erc20::makeTransaction(
                bcos::ref(inputs[iteration % benchmark_erc20::USER_COUNT]),
                fixture.m_contractAddress, fixture.m_spender, iteration);
            auto receipt = co_await fixture.m_executor.executeTransaction(fixture.m_backendStorage,
                fixture.blockHeader, transaction, 0, fixture.ledgerConfig, false);
            if (receipt->status() != 0)
            {
                state.SkipWithError(
                    fmt::format("transferFrom failed, status: {}", receipt->status()).c_str());
                break;
            }
            ++iteration;
        }
    }(state));
}

BENCHMARK(erc20_approve);
BENCHMARK(erc20_transferFrom);

BENCHMARK_MAIN();