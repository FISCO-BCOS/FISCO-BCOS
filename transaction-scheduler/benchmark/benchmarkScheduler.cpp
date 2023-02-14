#include <bcos-cpp-sdk/utilities/abi/ContractABICodec.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptImpl.h>
#include <bcos-task/Wait.h>
#include <bcos-transaction-executor/TransactionExecutorImpl.h>
#include <bcos-transaction-scheduler/MultiLayerStorage.h>
#include <bcos-transaction-scheduler/SchedulerSerialImpl.h>
#include <benchmark/benchmark.h>
#include <fmt/format.h>
#include <transaction-executor/tests/TestBytecode.h>
#include <range/v3/view/any_view.hpp>
#include <range/v3/view/single.hpp>

using namespace bcos;
using namespace bcos::storage2::memory_storage;
using namespace bcos::transaction_scheduler;
using namespace bcos::transaction_executor;

struct TableNameHash
{
    size_t operator()(const bcos::transaction_executor::StateKey& key) const
    {
        auto const& tableID = std::get<0>(key);
        return std::hash<bcos::transaction_executor::TableNameID>{}(tableID);
    }
};

using MutableStorage = MemoryStorage<StateKey, StateValue, Attribute(ORDERED | LOGICAL_DELETION)>;
using BackendStorage =
    MemoryStorage<StateKey, StateValue, Attribute(ORDERED | CONCURRENT), TableNameHash>;
using MultiLayerStorageType = MultiLayerStorage<MutableStorage, void, BackendStorage>;
using ReceiptFactory = bcostars::protocol::TransactionReceiptFactoryImpl;

bcos::crypto::Hash::Ptr bcos::transaction_executor::GlobalHashImpl::g_hashImpl;

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
        m_scheduler(m_multiLayerStorage, *m_receiptFactory, m_tableNamePool)
    {
        bcos::transaction_executor::GlobalHashImpl::g_hashImpl =
            std::make_shared<bcos::crypto::Keccak256>();
        boost::algorithm::unhex(helloworldBytecode, std::back_inserter(m_helloworldBytecodeBinary));
    }

    std::string deployContract()
    {
        std::string contractAddress;
        task::syncWait([this, &contractAddress]() -> task::Task<void> {
            bcostars::protocol::TransactionImpl createTransaction(
                [inner = bcostars::Transaction()]() mutable { return std::addressof(inner); });
            createTransaction.mutableInner().data.input.assign(
                m_helloworldBytecodeBinary.begin(), m_helloworldBytecodeBinary.end());

            auto block = m_blockFactory->createBlock();
            auto blockHeader = block->blockHeader();
            blockHeader->setNumber(1);
            blockHeader->calculateHash(*m_cryptoSuite->hashImpl());

            auto transactions =
                RANGES::single_view(std::addressof(createTransaction)) |
                RANGES::views::transform([](auto* ptr) -> auto const& { return *ptr; });
            m_scheduler.start();
            auto receipts = co_await m_scheduler.execute(*block->blockHeaderConst(), transactions);
            co_await m_scheduler.finish(*blockHeader, *(m_cryptoSuite->hashImpl()));
            co_await m_scheduler.commit();

            contractAddress = receipts[0]->contractAddress();
        }());

        return contractAddress;
    }

    void clear() { m_transactions.clear(); }

    void prepareIssue(size_t count)
    {
        task::syncWait([this](size_t count) -> task::Task<void> {
            std::mt19937_64 rng(std::random_device{}());

            // Generation accounts
            m_addresses = RANGES::iota_view<size_t, size_t>(0, count) |
                          RANGES::views::transform([&rng](size_t index) {
                              bcos::h160 address;
                              address.generateRandomFixedBytesByEngine(rng);
                              return address;
                          }) |
                          RANGES::to<decltype(m_addresses)>();

            bcos::codec::abi::ContractABICodec abiCodec(
                bcos::transaction_executor::GlobalHashImpl::g_hashImpl);
            m_transactions =
                m_addresses | RANGES::views::transform([&abiCodec](const Address& address) {
                    auto transaction = std::make_unique<bcostars::protocol::TransactionImpl>(
                        [inner = bcostars::Transaction()]() mutable {
                            return std::addressof(inner);
                        });
                    auto& inner = transaction->mutableInner();

                    auto input = abiCodec.abiIn("issue(address, int)", address, s256(10000));
                    inner.data.input.assign(input.begin(), input.end());
                    return transaction;
                }) |
                RANGES::to<decltype(m_transactions)>();
            co_return;
        }(count));
    }

    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;

    std::shared_ptr<bcostars::protocol::BlockHeaderFactoryImpl> m_blockHeaderFactory;
    std::shared_ptr<bcostars::protocol::TransactionFactoryImpl> m_transactionFactory;
    std::shared_ptr<bcostars::protocol::TransactionReceiptFactoryImpl> m_receiptFactory;
    std::shared_ptr<bcostars::protocol::BlockFactoryImpl> m_blockFactory;

    BackendStorage m_backendStorage;
    MultiLayerStorageType m_multiLayerStorage;

    TableNamePool m_tableNamePool;
    bcos::bytes m_helloworldBytecodeBinary;

    SchedulerSerialImpl<MultiLayerStorageType, ReceiptFactory,
        TransactionExecutorImpl<MultiLayerStorageType, ReceiptFactory>>
        m_scheduler;

    std::vector<Address> m_addresses;
    std::vector<std::unique_ptr<bcostars::protocol::TransactionImpl>> m_transactions;
};

static void serialScheduler(benchmark::State& state)
{
    Fixture fixture;
    fixture.deployContract();
    fixture.prepareIssue(state.range(0));

    int i = 1;
    task::syncWait([&](benchmark::State& state) -> task::Task<void> {
        for (auto const& it : state)
        {
            bcostars::protocol::BlockHeaderImpl blockHeader(
                [inner = bcostars::BlockHeader()]() mutable { return std::addressof(inner); });
            blockHeader.setNumber(i++);

            fixture.m_scheduler.start();
            [[maybe_unused]] auto receipts = co_await fixture.m_scheduler.execute(blockHeader,
                fixture.m_transactions |
                    RANGES::views::transform(
                        [](const std::unique_ptr<bcostars::protocol::TransactionImpl>& transaction)
                            -> auto& { return *transaction; }));
            co_await fixture.m_scheduler.finish(blockHeader, *(fixture.m_cryptoSuite->hashImpl()));
        }

        co_return;
    }(state));
}

// static void parallelScheduler(benchmark::State& state) {}

BENCHMARK(serialScheduler)->Arg(1000)->Arg(10000)->Arg(100000);