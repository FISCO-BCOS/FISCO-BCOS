/// @file benchmarkEthereumExecutor.cpp
/// @brief ERC-20 benchmark for the pure-Ethereum executor, mirroring
///        transaction-executor/benchmark/benchmakrExecutor.cpp.
///
/// Both benchmarks run the identical standard ERC-20 bytecode
/// (transaction-executor/benchmark/BenchmarkERC20.sol), the same 1000-user
/// account set and the same approve / transferFrom transaction stream, so the
/// numbers are directly comparable between TransactionExecutorImpl and
/// EthereumExecutor.
///
/// Differences vs. the transaction-executor fixture, all required by Ethereum
/// semantics:
///   - every transaction carries an explicit per-sender nonce (strict nonce
///     ordering) and an explicit gas limit;
///   - the deployed contract address is derived with CREATE
///     (keccak(rlp([sender, nonce]))[12..]) because the v2 receipt does not
///     carry a contract address;
///   - the ledger config pins the EVM revision to Shanghai, matching the
///     evm_version the contract was compiled for.

#include "../EthereumExecutor.h"
#include "../EVMSupport.h"
#include "../tests/TestMemoryStorage.h"
#include "benchmark/BenchmarkERC20.h"

#include "bcos-codec/bcos-codec/abi/ContractABICodec.h"
#include "bcos-crypto/interfaces/crypto/CryptoSuite.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-utilities/DataConvertUtility.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <benchmark/benchmark.h>
#include <boost/throw_exception.hpp>
#include <stdexcept>

using namespace bcos;
using namespace bcos::storage2::memory_storage;
using namespace bcos::executor_v1;

namespace eth = bcos::executor_v1::eth;

using ReceiptFactory = bcostars::protocol::TransactionReceiptFactoryImpl;

struct ERC20Fixture
{
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    MutableStorage m_backendStorage;
    ReceiptFactory m_receiptFactory;
    eth::EthereumExecutor m_executor;
    bcos::bytes m_erc20BytecodeBinary;

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
        m_executor(m_receiptFactory),
        m_abiCodec(*m_cryptoSuite->hashImpl())
    {
        boost::log::core::get()->set_logging_enabled(false);
        boost::algorithm::unhex(
            benchmark_erc20::erc20Bytecode, std::back_inserter(m_erc20BytecodeBinary));
        blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION);
        // The contract is compiled for Shanghai (PUSH0, no Cancun MCOPY /
        // TSTORE), matching the transaction-executor benchmark's EVM surface.
        ledgerConfig.setEVMCRevision(EVMC_SHANGHAI);
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
        // The v2 receipt carries no contract address, so derive it the CREATE
        // way for (deployer, nonce 0) up front.
        evmc::address deployerAddress{};
        std::copy_n(m_deployer.data(), sizeof(evmc::address), deployerAddress.bytes);
        const auto contractAddress = eth::evm::compute_create_address(deployerAddress, 0);
        m_contractAddress =
            bcos::toHexStringWithPrefix(bcos::bytesConstRef(contractAddress.bytes, 20));

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
