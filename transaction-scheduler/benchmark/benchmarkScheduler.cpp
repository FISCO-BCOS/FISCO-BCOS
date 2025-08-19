#include "bcos-codec/bcos-codec/abi/ContractABICodec.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-task/Wait.h"
#include "bcos-transaction-executor/TransactionExecutorImpl.h"
#include "bcos-transaction-executor/precompiled/PrecompiledManager.h"
#include "bcos-transaction-scheduler/MultiLayerStorage.h"
#include "bcos-transaction-scheduler/SchedulerParallelImpl.h"
#include "bcos-transaction-scheduler/SchedulerSerialImpl.h"
#include "transaction-executor/tests/TestBytecode.h"
#include <benchmark/benchmark.h>
#include <boost/throw_exception.hpp>
#include <random>
#include <range/v3/view/indirect.hpp>
#include <variant>

using namespace bcos;
using namespace bcos::storage2::memory_storage;
using namespace bcos::scheduler_v1;
using namespace bcos::executor_v1;

constexpr static s256 singleIssue = 1000000;
constexpr static s256 singleTransfer = 1;
constexpr static std::string_view transferMethod{"transfer(address,address,int256)"};

using MutableStorage = MemoryStorage<StateKey, StateValue, Attribute(ORDERED | LOGICAL_DELETION)>;
using BackendStorage = MemoryStorage<StateKey, StateValue, ORDERED | LRU | CONCURRENT>;
using MultiLayerStorageType = MultiLayerStorage<MutableStorage, void, BackendStorage>;
using ReceiptFactory = bcostars::protocol::TransactionReceiptFactoryImpl;

template <bool parallel>
struct Fixture
{
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    std::shared_ptr<bcostars::protocol::BlockHeaderFactoryImpl> m_blockHeaderFactory;
    std::shared_ptr<bcostars::protocol::TransactionFactoryImpl> m_transactionFactory;
    std::shared_ptr<bcostars::protocol::TransactionReceiptFactoryImpl> m_receiptFactory;
    std::shared_ptr<bcostars::protocol::BlockFactoryImpl> m_blockFactory;

    BackendStorage m_backendStorage;
    MultiLayerStorageType m_multiLayerStorage;
    bcos::bytes m_helloworldBytecodeBinary;

    PrecompiledManager m_precompiledManager;
    TransactionExecutorImpl m_executor;
    std::variant<std::monostate, SchedulerSerialImpl, SchedulerParallelImpl<MutableStorage>>
        m_scheduler;

    std::string m_contractAddress;
    std::vector<Address> m_addresses;
    std::vector<std::unique_ptr<bcostars::protocol::TransactionImpl>> m_transactions;
    struct Transfer
    {
        size_t from;
        size_t to;
    };
    std::vector<Transfer> m_transfers;
    ledger::LedgerConfig m_ledgerConfig;

    Fixture()
      : m_cryptoSuite(std::make_shared<bcos::crypto::CryptoSuite>(
            std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr)),
        m_blockHeaderFactory(
            std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(m_cryptoSuite)),
        m_transactionFactory(
            std::make_shared<bcostars::protocol::TransactionFactoryImpl>(m_cryptoSuite)),
        m_receiptFactory(
            std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(m_cryptoSuite)),
        m_blockFactory(std::make_shared<bcostars::protocol::BlockFactoryImpl>(
            m_cryptoSuite, m_blockHeaderFactory, m_transactionFactory, m_receiptFactory)),
        m_multiLayerStorage(m_backendStorage),
        m_precompiledManager(m_cryptoSuite->hashImpl()),
        m_executor(*m_receiptFactory, m_cryptoSuite->hashImpl(), m_precompiledManager)
    {
        boost::log::core::get()->set_logging_enabled(false);

        bcos::executor::GlobalHashImpl::g_hashImpl = std::make_shared<bcos::crypto::Keccak256>();
        boost::algorithm::unhex(helloworldBytecode, std::back_inserter(m_helloworldBytecodeBinary));

        if constexpr (parallel)
        {
            m_scheduler.emplace<SchedulerParallelImpl<MutableStorage>>();
        }
        else
        {
            m_scheduler.emplace<SchedulerSerialImpl>();
        }

        ledger::Features features;
        features.set(ledger::Features::Flag::feature_raw_address);
        m_ledgerConfig.setFeatures(features);
    }

    void deployContract()
    {
        std::visit(
            [this](auto& scheduler) {
                if constexpr (std::is_same_v<std::remove_cvref_t<decltype(scheduler)>,
                                  std::monostate>)
                {
                    BOOST_THROW_EXCEPTION(std::runtime_error("invalid scheduler"));
                }
                else
                {
                    task::syncWait([this, &scheduler]() -> task::Task<void> {
                        bcostars::protocol::TransactionImpl createTransaction;
                        createTransaction.mutableInner().data.input.assign(
                            m_helloworldBytecodeBinary.begin(), m_helloworldBytecodeBinary.end());
                        createTransaction.calculateHash(*m_cryptoSuite->hashImpl());

                        auto block = m_blockFactory->createBlock();
                        auto blockHeader = block->blockHeader();
                        blockHeader->setNumber(1);
                        blockHeader->setVersion(
                            (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION);
                        blockHeader->calculateHash(*m_cryptoSuite->hashImpl());

                        auto transactions =
                            ::ranges::single_view(&createTransaction) | ::ranges::views::indirect;

                        auto view = m_multiLayerStorage.fork();
                        view.newMutable();
                        auto receipts = co_await scheduler.executeBlock(view, m_executor,
                            *block->blockHeaderConst(), transactions, m_ledgerConfig);
                        if (receipts[0]->status() != 0)
                        {
                            fmt::print("deployContract unexpected receipt status: {}, {}\n",
                                receipts[0]->status(), receipts[0]->message());
                            co_return;
                        }
                        m_multiLayerStorage.pushView(std::move(view));
                        co_await m_multiLayerStorage.mergeBackStorage();

                        m_contractAddress = receipts[0]->contractAddress();
                    }());
                }
            },
            m_scheduler);
    }

    void prepareAddresses(size_t count)
    {
        std::mt19937_64 rng(std::random_device{}());

        // Generation accounts
        m_addresses = ::ranges::views::iota(0LU, count) |
                      ::ranges::views::transform([&rng](size_t index) {
                          bcos::h160 address;
                          address.generateRandomFixedBytesByEngine(rng);
                          return address;
                      }) |
                      ::ranges::to<decltype(m_addresses)>();
    }

    void prepareIssue(size_t count)
    {
        bcos::codec::abi::ContractABICodec abiCodec(*bcos::executor::GlobalHashImpl::g_hashImpl);
        m_transactions =
            m_addresses | ::ranges::views::transform([this, &abiCodec](const Address& address) {
                auto transaction = std::make_unique<bcostars::protocol::TransactionImpl>();
                auto& inner = transaction->mutableInner();

                inner.data.to = m_contractAddress;
                auto input = abiCodec.abiIn("issue(address,int256)", address, singleIssue);
                inner.data.input.assign(input.begin(), input.end());
                transaction->calculateHash(*m_cryptoSuite->hashImpl());
                return transaction;
            }) |
            ::ranges::to<decltype(m_transactions)>();
    }

    void prepareNoConflictTransfer()
    {
        bcos::codec::abi::ContractABICodec abiCodec(*bcos::executor::GlobalHashImpl::g_hashImpl);
        m_transactions =
            m_addresses | ::ranges::views::chunk(2) |
            ::ranges::views::transform([this, &abiCodec](auto&& range) {
                auto transaction = std::make_unique<bcostars::protocol::TransactionImpl>();
                auto& inner = transaction->mutableInner();
                inner.data.to = m_contractAddress;
                auto& fromAddress = range[0];
                auto& toAddress = range[1];

                auto input = abiCodec.abiIn(
                    "transfer(address,address,int256)", fromAddress, toAddress, singleTransfer);
                inner.data.input.assign(input.begin(), input.end());
                transaction->calculateHash(*m_cryptoSuite->hashImpl());
                return transaction;
            }) |
            ::ranges::to<decltype(m_transactions)>();
    }

    void prepareRandomTransfer()
    {
        bcos::codec::abi::ContractABICodec abiCodec(*bcos::executor::GlobalHashImpl::g_hashImpl);
        auto count = m_addresses.size();
        std::mt19937_64 rng(std::random_device{}());
        m_transfers.resize(count);
        m_transactions =
            ::ranges::views::transform(::ranges::views::iota(0LU, count),
                [&, this](size_t index) {
                    auto transaction = std::make_unique<bcostars::protocol::TransactionImpl>();
                    auto& inner = transaction->mutableInner();
                    inner.data.to = m_contractAddress;
                    auto& transfer = m_transfers[index];
                    transfer.from = rng() % count;
                    transfer.to = rng() % count;
                    auto& fromAddress = m_addresses[transfer.from];
                    auto& toAddress = m_addresses[transfer.to];

                    auto input = abiCodec.abiIn(
                        std::string(transferMethod), fromAddress, toAddress, singleTransfer);
                    inner.data.input.assign(input.begin(), input.end());
                    transaction->calculateHash(*m_cryptoSuite->hashImpl());
                    return transaction;
                }) |
            ::ranges::to<decltype(m_transactions)>();
    }

    void prepareConflictTransfer()
    {
        bcos::codec::abi::ContractABICodec abiCodec(*bcos::executor::GlobalHashImpl::g_hashImpl);
        m_transactions =
            ::ranges::views::zip(m_addresses, ::ranges::views::iota(0LU, m_addresses.size())) |
            ::ranges::views::transform([this, &abiCodec](auto&& tuple) {
                auto transaction = std::make_unique<bcostars::protocol::TransactionImpl>(
                    [inner = bcostars::Transaction()]() mutable { return std::addressof(inner); });
                auto& inner = transaction->mutableInner();
                inner.data.to = m_contractAddress;

                auto&& [toAddress, index] = tuple;
                auto fromAddress = toAddress;
                if (index > 0)
                {
                    fromAddress = m_addresses[index - 1];
                }

                auto input = abiCodec.abiIn(
                    "transfer(address,address,int256)", fromAddress, toAddress, singleTransfer);
                inner.data.input.assign(input.begin(), input.end());
                transaction->calculateHash(*m_cryptoSuite->hashImpl());
                return transaction;
            }) |
            ::ranges::to<decltype(m_transactions)>();
    }

    task::Task<std::vector<s256>> balances()
    {
        co_return co_await std::visit(
            [this](auto& scheduler) -> task::Task<std::vector<s256>> {
                if constexpr (std::is_same_v<std::remove_cvref_t<decltype(scheduler)>,
                                  std::monostate>)
                {
                    BOOST_THROW_EXCEPTION(std::runtime_error("invalid scheduler"));
                }
                else
                {
                    bcos::codec::abi::ContractABICodec abiCodec(
                        *bcos::executor::GlobalHashImpl::g_hashImpl);
                    // Verify the data
                    bcostars::protocol::BlockHeaderImpl blockHeader;
                    blockHeader.setNumber(0);
                    blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::MAX_VERSION);

                    auto checkTransactions =
                        ::ranges::views::transform(m_addresses,
                            [&](const auto& address) {
                                auto transaction =
                                    std::make_unique<bcostars::protocol::TransactionImpl>();
                                auto& inner = transaction->mutableInner();
                                inner.data.to = m_contractAddress;

                                auto input = abiCodec.abiIn("balance(address)", address);
                                inner.data.input.assign(input.begin(), input.end());
                                transaction->calculateHash(*m_cryptoSuite->hashImpl());
                                return transaction;
                            }) |
                        ::ranges::to<
                            std::vector<std::unique_ptr<bcostars::protocol::TransactionImpl>>>();

                    auto view = m_multiLayerStorage.fork();
                    view.newMutable();
                    auto receipts = co_await scheduler.executeBlock(view, m_executor, blockHeader,
                        ::ranges::views::indirect(checkTransactions), m_ledgerConfig);

                    auto balances = receipts |
                                    ::ranges::views::transform([&abiCodec](auto const& receipt) {
                                        if (receipt->status() != 0)
                                        {
                                            BOOST_THROW_EXCEPTION(std::runtime_error(
                                                fmt::format("Unexpected receipt status: {}, {}\n",
                                                    receipt->status(), receipt->message())));
                                        }

                                        s256 balance;
                                        abiCodec.abiOut(receipt->output(), balance);
                                        return balance;
                                    }) |
                                    ::ranges::to<std::vector<s256>>();

                    co_return balances;
                }
            },
            m_scheduler);
    }
};

static void initParallelScheduler(benchmark::State& state, auto& fixture)
{
    if (std::holds_alternative<SchedulerParallelImpl<MutableStorage>>(fixture.m_scheduler))
    {
        auto grainSize = state.range(1);
        auto maxParallel = state.range(2);
        auto& scheduler = std::get<SchedulerParallelImpl<MutableStorage>>(fixture.m_scheduler);
        if (grainSize > 0)
        {
            scheduler.m_grainSize = grainSize;
        }
        if (maxParallel > 0)
        {
            scheduler.m_maxConcurrency = maxParallel;
        }
    }
}

template <bool parallel>
static void noConflictTransfer(benchmark::State& state)
{
    Fixture<parallel> fixture;
    fixture.deployContract();
    initParallelScheduler(state, fixture);

    auto count = state.range(0) * 2;
    fixture.prepareAddresses(count);
    fixture.prepareIssue(count);

    std::visit(
        [&](auto& scheduler) {
            if constexpr (std::is_same_v<std::remove_cvref_t<decltype(scheduler)>, std::monostate>)
            {
                BOOST_THROW_EXCEPTION(std::runtime_error("invalid scheduler"));
            }
            else
            {
                int i = 0;
                task::syncWait([&](benchmark::State& state) -> task::Task<void> {
                    // First issue
                    bcostars::protocol::BlockHeaderImpl blockHeader;
                    blockHeader.setNumber(0);
                    blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION);

                    auto view = fixture.m_multiLayerStorage.fork();
                    view.newMutable();

                    [[maybe_unused]] auto receipts = co_await scheduler.executeBlock(view,
                        fixture.m_executor, blockHeader,
                        ::ranges::views::indirect(fixture.m_transactions), fixture.m_ledgerConfig);
                    fixture.m_transactions.clear();

                    fixture.prepareNoConflictTransfer();
                    // Start transfer
                    for (auto const& it : state)
                    {
                        bcostars::protocol::BlockHeaderImpl blockHeader;
                        blockHeader.setNumber((i++) + 1);
                        blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::MAX_VERSION);

                        [[maybe_unused]] auto receipts =
                            co_await scheduler.executeBlock(view, fixture.m_executor, blockHeader,
                                ::ranges::views::indirect(fixture.m_transactions),
                                fixture.m_ledgerConfig);
                    }

                    // Check
                    fixture.m_multiLayerStorage.pushView(std::move(view));
                    auto balances = co_await fixture.balances();
                    for (auto&& range : balances | ::ranges::views::chunk(2))
                    {
                        auto& from = range[0];
                        auto& to = range[1];

                        if (from != singleIssue - singleTransfer * i)
                        {
                            BOOST_THROW_EXCEPTION(std::runtime_error(
                                fmt::format("From balance not equal to expected! {}",
                                    from.template convert_to<std::string>())));
                        }

                        if (to != singleIssue + singleTransfer * i)
                        {
                            BOOST_THROW_EXCEPTION(std::runtime_error(
                                fmt::format("To balance not equal to expected! {}",
                                    to.template convert_to<std::string>())));
                        }
                    }
                    co_await fixture.m_multiLayerStorage.mergeBackStorage();
                }(state));
            }
        },
        fixture.m_scheduler);
}

template <bool parallel>
static void randomTransfer(benchmark::State& state)
{
    Fixture<parallel> fixture;
    fixture.deployContract();

    auto count = state.range(0);
    fixture.prepareAddresses(count);
    fixture.prepareIssue(count);

    initParallelScheduler(state, fixture);
    std::visit(
        [&](auto& scheduler) {
            if constexpr (std::is_same_v<std::remove_cvref_t<decltype(scheduler)>, std::monostate>)
            {
                BOOST_THROW_EXCEPTION(std::runtime_error("invalid scheduler"));
            }
            else
            {
                int i = 0;
                task::syncWait([&](benchmark::State& state) -> task::Task<void> {
                    // First issue
                    bcostars::protocol::BlockHeaderImpl blockHeader;
                    blockHeader.setNumber(0);
                    blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::MAX_VERSION);

                    auto view = fixture.m_multiLayerStorage.fork();
                    view.newMutable();

                    [[maybe_unused]] auto receipts = co_await scheduler.executeBlock(view,
                        fixture.m_executor, blockHeader,
                        ::ranges::views::indirect(fixture.m_transactions), fixture.m_ledgerConfig);
                    fixture.m_transactions.clear();

                    fixture.prepareRandomTransfer();
                    // Start transfer
                    for (auto const& it : state)
                    {
                        bcostars::protocol::BlockHeaderImpl blockHeader;
                        blockHeader.setNumber((i++) + 1);
                        blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::MAX_VERSION);

                        [[maybe_unused]] auto receipts =
                            co_await scheduler.executeBlock(view, fixture.m_executor, blockHeader,
                                ::ranges::views::indirect(fixture.m_transactions),
                                fixture.m_ledgerConfig);
                    }

                    // Check
                    fixture.m_multiLayerStorage.pushView(std::move(view));
                    auto balances = co_await fixture.balances();

                    std::vector<s256> expectbalances(count, singleIssue);
                    for (auto& transfer : fixture.m_transfers)
                    {
                        expectbalances[transfer.from] -= singleTransfer * i;
                        expectbalances[transfer.to] += singleTransfer * i;
                    }
                    for (auto&& [expect, got] : ::ranges::views::zip(expectbalances, balances))
                    {
                        if (expect != got)
                        {
                            BOOST_THROW_EXCEPTION(std::runtime_error(
                                fmt::format("From balance not equal to expected! {} {}",
                                    expect.str(), got.str())));
                        }
                    }
                    co_await fixture.m_multiLayerStorage.mergeBackStorage();
                }(state));
            }
        },
        fixture.m_scheduler);
}

template <bool parallel>
static void conflictTransfer(benchmark::State& state)
{
    Fixture<parallel> fixture;
    fixture.deployContract();

    auto count = state.range(0) * 2;
    fixture.prepareAddresses(count);
    fixture.prepareIssue(count);

    initParallelScheduler(state, fixture);
    std::visit(
        [&](auto& scheduler) {
            if constexpr (std::is_same_v<std::remove_cvref_t<decltype(scheduler)>, std::monostate>)
            {
                BOOST_THROW_EXCEPTION(std::runtime_error("invalid scheduler"));
            }
            else
            {
                int i = 0;
                auto view = fixture.m_multiLayerStorage.fork();
                view.newMutable();

                task::syncWait([&](benchmark::State& state) -> task::Task<void> {
                    // First issue
                    bcostars::protocol::BlockHeaderImpl blockHeader(
                        [inner = bcostars::BlockHeader()]() mutable {
                            return std::addressof(inner);
                        });
                    blockHeader.setNumber(0);
                    blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::MAX_VERSION);

                    [[maybe_unused]] auto receipts = co_await scheduler.executeBlock(view,
                        fixture.m_executor, blockHeader,
                        ::ranges::views::indirect(fixture.m_transactions), fixture.m_ledgerConfig);

                    fixture.m_transactions.clear();
                    fixture.prepareConflictTransfer();

                    // Start transfer
                    for (auto const& it : state)
                    {
                        bcostars::protocol::BlockHeaderImpl blockHeader;
                        blockHeader.setNumber((i++) + 1);
                        blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::MAX_VERSION);
                        [[maybe_unused]] auto receipts =
                            co_await scheduler.executeBlock(view, fixture.m_executor, blockHeader,
                                ::ranges::views::indirect(fixture.m_transactions),
                                fixture.m_ledgerConfig);
                    }

                    // Check
                    fixture.m_multiLayerStorage.pushView(std::move(view));
                    auto balances = co_await fixture.balances();
                    for (auto&& [balance, index] :
                        ::ranges::views::zip(balances, ::ranges::views::iota(0LU)))
                    {
                        if (index == 0)
                        {
                            if (balance != singleIssue - i * singleTransfer)
                            {
                                BOOST_THROW_EXCEPTION(std::runtime_error(
                                    fmt::format("Start balance not equal to expected! {} {}", index,
                                        balance.template convert_to<std::string>())));
                            }
                        }
                        else if (index == balances.size() - 1)
                        {
                            if (balance != singleIssue + i * singleTransfer)
                            {
                                BOOST_THROW_EXCEPTION(std::runtime_error(
                                    fmt::format("End balance not equal to expected! {} {}", index,
                                        balance.template convert_to<std::string>())));
                            }
                        }
                        else
                        {
                            if (balance != singleIssue)
                            {
                                BOOST_THROW_EXCEPTION(std::runtime_error(
                                    fmt::format("Balance not equal to expected! {} {}", index,
                                        balance.template convert_to<std::string>())));
                            }
                        }
                    }
                    co_await fixture.m_multiLayerStorage.mergeBackStorage();
                }(state));
            }
        },
        fixture.m_scheduler);
}

constexpr static bool SERIAL = false;
constexpr static bool PARALLEL = true;

BENCHMARK(noConflictTransfer<SERIAL>)->Arg(1000)->Arg(10000)->Arg(100000);
BENCHMARK(noConflictTransfer<PARALLEL>)
    ->Args({1000, 16, 4})
    ->Args({1000, 16, 6})
    ->Args({1000, 16, 8})
    ->Args({1000, 16, 16})
    ->Args({1000, 64, 4})
    ->Args({1000, 64, 6})
    ->Args({1000, 64, 8})
    ->Args({1000, 64, 16})
    ->Args({1000, 256, 4})
    ->Args({1000, 256, 6})
    ->Args({1000, 256, 8})
    ->Args({1000, 256, 16})
    ->Args({10000, 16, 4})
    ->Args({10000, 16, 6})
    ->Args({10000, 16, 8})
    ->Args({10000, 16, 16})
    ->Args({10000, 64, 4})
    ->Args({10000, 64, 6})
    ->Args({10000, 64, 8})
    ->Args({10000, 64, 16})
    ->Args({10000, 256, 4})
    ->Args({10000, 256, 6})
    ->Args({10000, 256, 8})
    ->Args({10000, 256, 16})
    ->Args({100000, 16, 4})
    ->Args({100000, 16, 6})
    ->Args({100000, 16, 8})
    ->Args({100000, 16, 16})
    ->Args({100000, 64, 4})
    ->Args({100000, 64, 6})
    ->Args({100000, 64, 8})
    ->Args({100000, 64, 16})
    ->Args({100000, 256, 4})
    ->Args({100000, 256, 6})
    ->Args({100000, 256, 8})
    ->Args({100000, 256, 16});

BENCHMARK(randomTransfer<SERIAL>)->Arg(1000)->Arg(10000)->Arg(100000);
BENCHMARK(randomTransfer<PARALLEL>)
    ->Args({1000, 16, 4})
    ->Args({1000, 16, 6})
    ->Args({1000, 16, 8})
    ->Args({1000, 16, 16})
    ->Args({1000, 64, 4})
    ->Args({1000, 64, 6})
    ->Args({1000, 64, 8})
    ->Args({1000, 64, 16})
    ->Args({1000, 256, 4})
    ->Args({1000, 256, 6})
    ->Args({1000, 256, 8})
    ->Args({1000, 256, 16})
    ->Args({10000, 16, 4})
    ->Args({10000, 16, 6})
    ->Args({10000, 16, 8})
    ->Args({10000, 16, 16})
    ->Args({10000, 64, 4})
    ->Args({10000, 64, 6})
    ->Args({10000, 64, 8})
    ->Args({10000, 64, 16})
    ->Args({10000, 256, 4})
    ->Args({10000, 256, 6})
    ->Args({10000, 256, 8})
    ->Args({10000, 256, 16})
    ->Args({100000, 16, 4})
    ->Args({100000, 16, 6})
    ->Args({100000, 16, 8})
    ->Args({100000, 16, 16})
    ->Args({100000, 64, 4})
    ->Args({100000, 64, 6})
    ->Args({100000, 64, 8})
    ->Args({100000, 64, 16})
    ->Args({100000, 256, 4})
    ->Args({100000, 256, 6})
    ->Args({100000, 256, 8})
    ->Args({100000, 256, 16});

BENCHMARK(conflictTransfer<SERIAL>)->Arg(1000)->Arg(10000)->Arg(100000);
BENCHMARK(conflictTransfer<PARALLEL>)
    ->Args({1000, 16, 4})
    ->Args({1000, 16, 6})
    ->Args({1000, 16, 8})
    ->Args({1000, 16, 16})
    ->Args({1000, 64, 4})
    ->Args({1000, 64, 6})
    ->Args({1000, 64, 8})
    ->Args({1000, 64, 16})
    ->Args({1000, 256, 4})
    ->Args({1000, 256, 6})
    ->Args({1000, 256, 8})
    ->Args({1000, 256, 16})
    ->Args({10000, 16, 4})
    ->Args({10000, 16, 6})
    ->Args({10000, 16, 8})
    ->Args({10000, 16, 16})
    ->Args({10000, 64, 4})
    ->Args({10000, 64, 6})
    ->Args({10000, 64, 8})
    ->Args({10000, 64, 16})
    ->Args({10000, 256, 4})
    ->Args({10000, 256, 6})
    ->Args({10000, 256, 8})
    ->Args({10000, 256, 16})
    ->Args({100000, 16, 4})
    ->Args({100000, 16, 6})
    ->Args({100000, 16, 8})
    ->Args({100000, 16, 16})
    ->Args({100000, 64, 4})
    ->Args({100000, 64, 6})
    ->Args({100000, 64, 8})
    ->Args({100000, 64, 16})
    ->Args({100000, 256, 4})
    ->Args({100000, 256, 6})
    ->Args({100000, 256, 8})
    ->Args({100000, 256, 16});