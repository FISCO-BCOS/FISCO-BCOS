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

using Storage = MemoryStorage<StateKey, StateValue, ORDERED>;
using BackendStorage =
    MemoryStorage<StateKey, StateValue, Attribute(ORDERED | CONCURRENT), TableNameHash>;
using ReceiptFactory = bcostars::protocol::TransactionReceiptFactoryImpl;

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
        m_scheduler(m_backendStorage, *m_receiptFactory, m_tableNamePool)
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
            blockHeader->setNumber(100);
            blockHeader->calculateHash(*m_cryptoSuite->hashImpl());

            // block->appendTransaction(std::make_shared<bcostars::protocol::TransactionImpl>(
            //     std::move(createTransaction)));

            auto transactions = RANGES::single_view(std::addressof(createTransaction)) |
                                RANGES::views::transform([](auto* ptr) -> auto& { return *ptr; });
            auto receipts = co_await m_scheduler.execute(*block->blockHeaderConst(), transactions);
            co_await m_scheduler.commit();

            contractAddress = receipts[0]->contractAddress();
        }());

        return contractAddress;
    }

    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;

    std::shared_ptr<bcostars::protocol::BlockHeaderFactoryImpl> m_blockHeaderFactory;
    std::shared_ptr<bcostars::protocol::TransactionFactoryImpl> m_transactionFactory;
    std::shared_ptr<bcostars::protocol::TransactionReceiptFactoryImpl> m_receiptFactory;
    std::shared_ptr<bcostars::protocol::BlockFactoryImpl> m_blockFactory;

    BackendStorage m_backendStorage;
    TableNamePool m_tableNamePool;
    bcos::bytes m_helloworldBytecodeBinary;

    SchedulerSerialImpl<BackendStorage, ReceiptFactory,
        TransactionExecutorImpl<Storage, ReceiptFactory>>
        m_scheduler;
};

static void serialExecute(benchmark::State& state)
{
    Fixture fixture;
    fixture.deployContract();
}