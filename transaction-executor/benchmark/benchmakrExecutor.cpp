#include "../bcos-transaction-executor/TransactionExecutorImpl.h"
#include "../tests/TestBytecode.h"
#include "bcos-crypto/interfaces/crypto/CryptoSuite.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include <bcos-cpp-sdk/utilities/abi/ContractABICodec.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-task/Wait.h>
#include <benchmark/benchmark.h>

using namespace bcos;
using namespace bcos::storage2::memory_storage;
using namespace bcos::transaction_executor;

using Storage = MemoryStorage<StateKey, StateValue, ORDERED>;
using ReceiptFactory = bcostars::protocol::TransactionReceiptFactoryImpl;

bcos::crypto::Hash::Ptr bcos::transaction_executor::GlobalHashImpl::g_hashImpl;

struct Fixture
{
    Fixture()
      : cryptoSuite(std::make_shared<bcos::crypto::CryptoSuite>(
            std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr)),
        receiptFactory(cryptoSuite),
        executor(storage, receiptFactory, tableNamePool),
        blockHeader([inner = std::addressof(tarsBlockHeader)]() mutable { return inner; })
    {
        bcos::transaction_executor::GlobalHashImpl::g_hashImpl =
            std::make_shared<bcos::crypto::Keccak256>();
        boost::algorithm::unhex(helloworldBytecode, std::back_inserter(helloworldBytecodeBinary));
    }

    std::string deployContract()
    {
        std::string contractAddress;
        task::syncWait([this, &contractAddress]() -> task::Task<void> {
            bcostars::protocol::TransactionImpl createTransaction(
                [inner = bcostars::Transaction()]() mutable { return std::addressof(inner); });
            createTransaction.mutableInner().data.input.assign(
                helloworldBytecodeBinary.begin(), helloworldBytecodeBinary.end());
            auto receipt = co_await executor.execute(blockHeader, createTransaction, 0);
            contractAddress = receipt->contractAddress();
        }());

        return contractAddress;
    }

    bcos::crypto::CryptoSuite::Ptr cryptoSuite;
    Storage storage;
    ReceiptFactory receiptFactory;
    TableNamePool tableNamePool;
    bcos::transaction_executor::TransactionExecutorImpl<Storage, ReceiptFactory> executor;
    bcos::bytes helloworldBytecodeBinary;

    bcostars::BlockHeader tarsBlockHeader;
    bcostars::protocol::BlockHeaderImpl blockHeader;
};

static void create(benchmark::State& state)
{
    Fixture fixture;

    bcostars::protocol::TransactionImpl transaction(
        [inner = bcostars::Transaction()]() mutable { return std::addressof(inner); });
    transaction.mutableInner().data.input.assign(
        fixture.helloworldBytecodeBinary.begin(), fixture.helloworldBytecodeBinary.end());

    task::syncWait([&fixture](benchmark::State& state,
                       decltype(transaction)& transaction) -> task::Task<void> {
        int contextID = 0;
        for (auto const& it : state)
        {
            [[maybe_unused]] auto receipt =
                co_await fixture.executor.execute(fixture.blockHeader, transaction, ++contextID);
        }
    }(state, transaction));
}

static void call_setInt(benchmark::State& state)
{
    Fixture fixture;
    std::string contractAddress = fixture.deployContract();

    bcostars::protocol::TransactionImpl transaction(
        [inner = bcostars::Transaction()]() mutable { return std::addressof(inner); });

    bcos::codec::abi::ContractABICodec abiCodec(
        bcos::transaction_executor::GlobalHashImpl::g_hashImpl);
    auto input = abiCodec.abiIn("setInt(int256)", bcos::s256(10000));
    transaction.mutableInner().data.input.assign(input.begin(), input.end());
    transaction.mutableInner().data.to = contractAddress;

    task::syncWait([&](benchmark::State& state) -> task::Task<void> {
        int contextID = 0;
        for (auto const& it : state)
        {
            [[maybe_unused]] auto receipt =
                co_await fixture.executor.execute(fixture.blockHeader, transaction, ++contextID);
        }
    }(state));
}

static void call_setString(benchmark::State& state)
{
    Fixture fixture;
    std::string contractAddress = fixture.deployContract();

    bcostars::protocol::TransactionImpl transaction(
        [inner = bcostars::Transaction()]() mutable { return std::addressof(inner); });

    bcos::codec::abi::ContractABICodec abiCodec(
        bcos::transaction_executor::GlobalHashImpl::g_hashImpl);
    auto input = abiCodec.abiIn("setString(string)", std::string("Hello world, fisco-bcos!"));
    transaction.mutableInner().data.input.assign(input.begin(), input.end());
    transaction.mutableInner().data.to = contractAddress;

    task::syncWait([&](benchmark::State& state) -> task::Task<void> {
        int contextID = 0;
        for (auto const& it : state)
        {
            [[maybe_unused]] auto receipt =
                co_await fixture.executor.execute(fixture.blockHeader, transaction, ++contextID);
        }
    }(state));
}

static void call_delegateCall(benchmark::State& state)
{
    Fixture fixture;
    std::string contractAddress = fixture.deployContract();

    bcos::codec::abi::ContractABICodec abiCodec(
        bcos::transaction_executor::GlobalHashImpl::g_hashImpl);
    bcostars::protocol::TransactionImpl transaction1(
        [inner = bcostars::Transaction()]() mutable { return std::addressof(inner); });
    auto input = abiCodec.abiIn("delegateCall()");
    transaction1.mutableInner().data.input.assign(input.begin(), input.end());
    transaction1.mutableInner().data.to = contractAddress;

    task::syncWait([&](benchmark::State& state) -> task::Task<void> {
        int contextID = 0;
        for (auto const& it : state)
        {
            [[maybe_unused]] auto receipt =
                co_await fixture.executor.execute(fixture.blockHeader, transaction1, ++contextID);
        }
    }(state));
}

static void call_deployAndCall(benchmark::State& state)
{
    Fixture fixture;
    std::string contractAddress = fixture.deployContract();

    bcos::codec::abi::ContractABICodec abiCodec(
        bcos::transaction_executor::GlobalHashImpl::g_hashImpl);
    bcostars::protocol::TransactionImpl transaction1(
        [inner = bcostars::Transaction()]() mutable { return std::addressof(inner); });
    auto input = abiCodec.abiIn("deployAndCall(int256)", bcos::s256(999));
    transaction1.mutableInner().data.input.assign(input.begin(), input.end());
    transaction1.mutableInner().data.to = contractAddress;

    task::syncWait([&](benchmark::State& state) -> task::Task<void> {
        int contextID = 0;
        for (auto const& it : state)
        {
            [[maybe_unused]] auto receipt =
                co_await fixture.executor.execute(fixture.blockHeader, transaction1, ++contextID);
        }
    }(state));
}

BENCHMARK(create);
BENCHMARK(call_setInt);
BENCHMARK(call_setString);
BENCHMARK(call_delegateCall);
BENCHMARK(call_deployAndCall);

BENCHMARK_MAIN();