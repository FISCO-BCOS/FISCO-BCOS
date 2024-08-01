#include "bcos-codec/bcos-codec/abi/ContractABICodec.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/storage2/MemoryStorage.h"
#include "bcos-tars-protocol/protocol/BlockFactoryImpl.h"
#include "bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionFactoryImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include "bcos-task/Wait.h"
#include "bcos-transaction-executor/TransactionExecutorImpl.h"
#include "bcos-transaction-scheduler/MultiLayerStorage.h"
#include "bcos-transaction-scheduler/SchedulerParallelImpl.h"
#include "bcos-transaction-scheduler/SchedulerSerialImpl.h"
#include "transaction-executor/tests/TestBytecode.h"
#include <benchmark/benchmark.h>
#include <boost/throw_exception.hpp>

using namespace bcos;
using namespace bcos::storage2::memory_storage;
using namespace bcos::transaction_scheduler;
using namespace bcos::transaction_executor;

constexpr static s256 singleIssue(1000000);
constexpr static s256 singleTransfer(1);

using MutableStorage = MemoryStorage<StateKey, StateValue, Attribute(ORDERED | LOGICAL_DELETION)>;
using BackendStorage = MemoryStorage<StateKey, StateValue, Attribute(ORDERED | LRU)>;
using MultiLayerStorageType = MultiLayerStorage<MutableStorage, void, BackendStorage>;
using ReceiptFactory = bcostars::protocol::TransactionReceiptFactoryImpl;

template <bool parallel>
struct Fixture
{
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
        m_executor(*m_receiptFactory, m_cryptoSuite->hashImpl())
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
                        bcostars::protocol::TransactionImpl createTransaction(
                            [inner = bcostars::Transaction()]() mutable {
                                return std::addressof(inner);
                            });
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
                            RANGES::single_view(std::addressof(createTransaction)) |
                            RANGES::views::transform([](auto* ptr) -> auto const& { return *ptr; });

                        auto view = fork(m_multiLayerStorage);
                        newMutable(view);
                        ledger::LedgerConfig ledgerConfig;
                        auto receipts =
                            co_await transaction_scheduler::executeBlock(scheduler, view,
                                m_executor, *block->blockHeaderConst(), transactions, ledgerConfig);
                        if (receipts[0]->status() != 0)
                        {
                            fmt::print("deployContract unexpected receipt status: {}, {}\n",
                                receipts[0]->status(), receipts[0]->message());
                            co_return;
                        }
                        pushView(m_multiLayerStorage, std::move(view));
                        co_await mergeBackStorage(m_multiLayerStorage);

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
        m_addresses = RANGES::views::iota(0LU, count) |
                      RANGES::views::transform([&rng](size_t index) {
                          bcos::h160 address;
                          address.generateRandomFixedBytesByEngine(rng);
                          return address;
                      }) |
                      RANGES::to<decltype(m_addresses)>();
    }

    void prepareIssue(size_t count)
    {
        bcos::codec::abi::ContractABICodec abiCodec(*bcos::executor::GlobalHashImpl::g_hashImpl);
        m_transactions =
            m_addresses | RANGES::views::transform([this, &abiCodec](const Address& address) {
                auto transaction = std::make_unique<bcostars::protocol::TransactionImpl>(
                    [inner = bcostars::Transaction()]() mutable { return std::addressof(inner); });
                auto& inner = transaction->mutableInner();

                inner.data.to = m_contractAddress;
                auto input = abiCodec.abiIn("issue(address,int256)", address, singleIssue);
                inner.data.input.assign(input.begin(), input.end());
                transaction->calculateHash(*m_cryptoSuite->hashImpl());
                return transaction;
            }) |
            RANGES::to<decltype(m_transactions)>();
    }

    void prepareTransfer(size_t count)
    {
        bcos::codec::abi::ContractABICodec abiCodec(*bcos::executor::GlobalHashImpl::g_hashImpl);
        m_transactions =
            m_addresses | RANGES::views::chunk(2) |
            RANGES::views::transform([this, &abiCodec](auto&& range) {
                auto transaction = std::make_unique<bcostars::protocol::TransactionImpl>(
                    [inner = bcostars::Transaction()]() mutable { return std::addressof(inner); });
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
            RANGES::to<decltype(m_transactions)>();
    }

    void prepareConflictTransfer(size_t count)
    {
        bcos::codec::abi::ContractABICodec abiCodec(*bcos::executor::GlobalHashImpl::g_hashImpl);
        m_transactions =
            RANGES::views::zip(m_addresses, RANGES::views::iota(0LU, m_addresses.size())) |
            RANGES::views::transform([this, &abiCodec](auto&& tuple) {
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
            RANGES::to<decltype(m_transactions)>();
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
                    bcostars::protocol::BlockHeaderImpl blockHeader(
                        [inner = bcostars::BlockHeader()]() mutable {
                            return std::addressof(inner);
                        });
                    blockHeader.setNumber(0);
                    blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION);

                    auto checkTransactions =
                        m_addresses | RANGES::views::transform([&](const auto& address) {
                            auto transaction =
                                std::make_unique<bcostars::protocol::TransactionImpl>(
                                    [inner = bcostars::Transaction()]() mutable {
                                        return std::addressof(inner);
                                    });
                            auto& inner = transaction->mutableInner();
                            inner.data.to = m_contractAddress;

                            auto input = abiCodec.abiIn("balance(address)", address);
                            inner.data.input.assign(input.begin(), input.end());
                            transaction->calculateHash(*m_cryptoSuite->hashImpl());
                            return transaction;
                        }) |
                        RANGES::to<
                            std::vector<std::unique_ptr<bcostars::protocol::TransactionImpl>>>();

                    auto view = fork(m_multiLayerStorage);
                    newMutable(view);
                    ledger::LedgerConfig ledgerConfig;
                    auto receipts = co_await transaction_scheduler::executeBlock(scheduler, view,
                        m_executor, blockHeader,
                        checkTransactions |
                            RANGES::views::transform(
                                [](const std::unique_ptr<bcostars::protocol::TransactionImpl>&
                                        transaction) -> auto& { return *transaction; }),
                        ledgerConfig);

                    auto balances = receipts |
                                    RANGES::views::transform([&abiCodec](auto const& receipt) {
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
                                    RANGES::to<std::vector<s256>>();

                    co_return balances;
                }
            },
            m_scheduler);
    }

    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    std::shared_ptr<bcostars::protocol::BlockHeaderFactoryImpl> m_blockHeaderFactory;
    std::shared_ptr<bcostars::protocol::TransactionFactoryImpl> m_transactionFactory;
    std::shared_ptr<bcostars::protocol::TransactionReceiptFactoryImpl> m_receiptFactory;
    std::shared_ptr<bcostars::protocol::BlockFactoryImpl> m_blockFactory;

    BackendStorage m_backendStorage;
    MultiLayerStorageType m_multiLayerStorage;
    bcos::bytes m_helloworldBytecodeBinary;

    TransactionExecutorImpl m_executor;
    std::variant<std::monostate, SchedulerSerialImpl, SchedulerParallelImpl<MutableStorage>>
        m_scheduler;

    std::string m_contractAddress;
    std::vector<Address> m_addresses;
    std::vector<std::unique_ptr<bcostars::protocol::TransactionImpl>> m_transactions;
};

template <bool parallel = false>
static void issue(benchmark::State& state)
{
    Fixture<parallel> fixture;
    fixture.deployContract();

    auto count = state.range(0);
    fixture.prepareAddresses(count);
    fixture.prepareIssue(count);

    int i = 0;
    std::visit(
        [&](auto& scheduler) {
            if constexpr (std::is_same_v<std::remove_cvref_t<decltype(scheduler)>, std::monostate>)
            {
                BOOST_THROW_EXCEPTION(std::runtime_error("invalid scheduler"));
            }
            else
            {
                task::syncWait([&](benchmark::State& state) -> task::Task<void> {
                    auto view = fork(fixture.m_multiLayerStorage);
                    newMutable(view);
                    for (auto const& it : state)
                    {
                        bcostars::protocol::BlockHeaderImpl blockHeader(
                            [inner = bcostars::BlockHeader()]() mutable {
                                return std::addressof(inner);
                            });
                        blockHeader.setNumber((i++) + 1);
                        blockHeader.setVersion(
                            (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION);
                        ledger::LedgerConfig ledgerConfig;
                        [[maybe_unused]] auto receipts =
                            co_await transaction_scheduler::executeBlock(scheduler, view,
                                fixture.m_executor, blockHeader,
                                fixture.m_transactions |
                                    RANGES::views::transform(
                                        [](const std::unique_ptr<
                                            bcostars::protocol::TransactionImpl>& transaction)
                                            -> auto& { return *transaction; }),
                                ledgerConfig);
                    }

                    pushView(fixture.m_multiLayerStorage, std::move(view));
                    auto balances = co_await fixture.balances();
                    for (auto& balance : balances)
                    {
                        if (balance != singleIssue * i)
                        {
                            BOOST_THROW_EXCEPTION(
                                std::runtime_error(fmt::format("Balance not equal to expected! {}",
                                    balance.template convert_to<std::string>())));
                        }
                    }
                    co_await mergeBackStorage(fixture.m_multiLayerStorage);
                }(state));
            }
        },
        fixture.m_scheduler);
}

template <bool parallel>
static void transfer(benchmark::State& state)
{
    Fixture<parallel> fixture;
    fixture.deployContract();

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
                    bcostars::protocol::BlockHeaderImpl blockHeader(
                        [inner = bcostars::BlockHeader()]() mutable {
                            return std::addressof(inner);
                        });
                    blockHeader.setNumber(0);
                    blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION);

                    auto view = fork(fixture.m_multiLayerStorage);
                    newMutable(view);
                    ledger::LedgerConfig ledgerConfig;
                    [[maybe_unused]] auto receipts = co_await transaction_scheduler::executeBlock(
                        scheduler, view, fixture.m_executor, blockHeader,
                        fixture.m_transactions |
                            RANGES::views::transform(
                                [](const std::unique_ptr<bcostars::protocol::TransactionImpl>&
                                        transaction) -> auto& { return *transaction; }),
                        ledgerConfig);

                    fixture.m_transactions.clear();
                    fixture.prepareTransfer(count);

                    // Start transfer
                    for (auto const& it : state)
                    {
                        bcostars::protocol::BlockHeaderImpl blockHeader(
                            [inner = bcostars::BlockHeader()]() mutable {
                                return std::addressof(inner);
                            });
                        blockHeader.setNumber((i++) + 1);
                        blockHeader.setVersion(
                            (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION);
                        ledger::LedgerConfig ledgerConfig;
                        [[maybe_unused]] auto receipts =
                            co_await transaction_scheduler::executeBlock(scheduler, view,
                                fixture.m_executor, blockHeader,
                                fixture.m_transactions |
                                    RANGES::views::transform(
                                        [](const std::unique_ptr<
                                            bcostars::protocol::TransactionImpl>& transaction)
                                            -> auto& { return *transaction; }),
                                ledgerConfig);
                    }

                    // Check
                    pushView(fixture.m_multiLayerStorage, std::move(view));
                    auto balances = co_await fixture.balances();
                    for (auto&& range : balances | RANGES::views::chunk(2))
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
                    co_await mergeBackStorage(fixture.m_multiLayerStorage);
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

    std::visit(
        [&](auto& scheduler) {
            if constexpr (std::is_same_v<std::remove_cvref_t<decltype(scheduler)>, std::monostate>)
            {
                BOOST_THROW_EXCEPTION(std::runtime_error("invalid scheduler"));
            }
            else
            {
                int i = 0;
                auto view = fork(fixture.m_multiLayerStorage);
                newMutable(view);

                task::syncWait([&](benchmark::State& state) -> task::Task<void> {
                    // First issue
                    bcostars::protocol::BlockHeaderImpl blockHeader(
                        [inner = bcostars::BlockHeader()]() mutable {
                            return std::addressof(inner);
                        });
                    blockHeader.setNumber(0);
                    blockHeader.setVersion((uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION);
                    ledger::LedgerConfig ledgerConfig;
                    [[maybe_unused]] auto receipts = co_await transaction_scheduler::executeBlock(
                        scheduler, view, fixture.m_executor, blockHeader,
                        fixture.m_transactions |
                            RANGES::views::transform(
                                [](const std::unique_ptr<bcostars::protocol::TransactionImpl>&
                                        transaction) -> auto& { return *transaction; }),
                        ledgerConfig);

                    fixture.m_transactions.clear();
                    fixture.prepareConflictTransfer(count);

                    // Start transfer
                    for (auto const& it : state)
                    {
                        bcostars::protocol::BlockHeaderImpl blockHeader(
                            [inner = bcostars::BlockHeader()]() mutable {
                                return std::addressof(inner);
                            });
                        blockHeader.setNumber((i++) + 1);
                        blockHeader.setVersion(
                            (uint32_t)bcos::protocol::BlockVersion::V3_1_VERSION);
                        ledger::LedgerConfig ledgerConfig;
                        [[maybe_unused]] auto receipts =
                            co_await transaction_scheduler::executeBlock(scheduler, view,
                                fixture.m_executor, blockHeader,
                                fixture.m_transactions |
                                    RANGES::views::transform(
                                        [](const std::unique_ptr<
                                            bcostars::protocol::TransactionImpl>& transaction)
                                            -> auto& { return *transaction; }),
                                ledgerConfig);
                    }

                    // Check
                    pushView(fixture.m_multiLayerStorage, std::move(view));
                    auto balances = co_await fixture.balances();
                    for (auto&& [balance, index] :
                        RANGES::views::zip(balances, RANGES::views::iota(0LU)))
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
                    co_await mergeBackStorage(fixture.m_multiLayerStorage);
                }(state));
            }
        },
        fixture.m_scheduler);
}

// static void parallelScheduler(benchmark::State& state) {}
constexpr static bool SERIAL = false;
constexpr static bool PARALLEL = true;

BENCHMARK(issue<SERIAL>)->Arg(1000)->Arg(10000)->Arg(100000);
BENCHMARK(issue<PARALLEL>)->Arg(1000)->Arg(10000)->Arg(100000);

BENCHMARK(transfer<SERIAL>)->Arg(1000)->Arg(10000)->Arg(100000);
BENCHMARK(transfer<PARALLEL>)->Arg(1000)->Arg(10000)->Arg(100000);

BENCHMARK(conflictTransfer<SERIAL>)->Arg(1000)->Arg(10000)->Arg(100000);
BENCHMARK(conflictTransfer<PARALLEL>)->Arg(1000)->Arg(10000)->Arg(100000);