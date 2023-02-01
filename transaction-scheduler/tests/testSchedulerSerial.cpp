#include "bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-transaction-executor/TransactionExecutorImpl.h>
#include <bcos-transaction-scheduler/SchedulerSerialImpl.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::storage2;
using namespace bcos::transaction_executor;
using namespace bcos::transaction_scheduler;

class TestSchedulerSerialFixture
{
public:
    using BackendStorage = memory_storage::MemoryStorage<StateKey, StateValue,
        memory_storage::Attribute(memory_storage::ORDERED | memory_storage::CONCURRENT),
        std::hash<StateKey>>;

    TestSchedulerSerialFixture() : receiptFactory(cryptoSuite), {}

    TableNamePool tableNamePool;
    BackendStorage backendStorage;
    bcos::crypto::CryptoSuite::Ptr cryptoSuite;
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory;
    SchedulerSerialImpl<BackendStorage, decltype(receiptFactory),
        TransactionExecutorImpl<BackendStorage, decltype(receiptFactory)>>
        scheduler;
};

BOOST_FIXTURE_TEST_SUITE(TestSchedulerSerial, TestSchedulerSerialFixture)

BOOST_AUTO_TEST_CASE(execute)
{
    auto hash = std::make_shared<bcos::crypto::Keccak256>();
    BackendStorage backendStorage;
    auto cryptoSuite = std::make_shared<bcos::crypto::CryptoSuite>(hash, nullptr, nullptr);
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory(cryptoSuite);

    SchedulerSerialImpl<BackendStorage, decltype(receiptFactory),
        TransactionExecutorImpl<BackendStorage, decltype(receiptFactory)>>
        scheduler(backendStorage, receiptFactory);
}

BOOST_AUTO_TEST_SUITE_END()