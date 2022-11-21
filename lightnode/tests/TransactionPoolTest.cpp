#include "bcos-task/Wait.h"
#include <bcos-tars-protocol/impl/TarsSerializable.h>

#include <bcos-lightnode/transaction_pool/TransactionPoolImpl.h>
#include <bcos-tars-protocol/protocol/TransactionSubmitResultImpl.h>
#include <bcos-tars-protocol/tars/Transaction.h>
#include <bcos-tars-protocol/tars/TransactionReceipt.h>
#include <bcos-task/Task.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/throw_exception.hpp>
#include <exception>
#include <thread>
#include <variant>

template <bool withError>
class MockTransactionPoolMT
{
public:
    bcos::task::Task<bcos::protocol::TransactionSubmitResult::Ptr> submitTransaction(
        bcos::protocol::Transaction::Ptr transaction)
    {
        if constexpr (withError)
        {
            auto error = std::make_shared<bcos::Error>(-1, "mock error!");
            BOOST_THROW_EXCEPTION(*error);
        }
        else
        {
            auto result =
                std::make_shared<bcostars::protocol::TransactionSubmitResultImpl>(nullptr);
            bcostars::TransactionReceipt receipt;
            receipt.data.status = 100;
            receipt.data.blockNumber = 10086;
            auto receiptObj = std::make_shared<bcostars::protocol::TransactionReceiptImpl>(
                nullptr, [receipt = std::move(receipt)]() mutable { return &receipt; });
            result->setTransactionReceipt(receiptObj);
            co_return result;
        }
    }
};

class MockTransactionPoolST
{
public:
    bcos::task::Task<bcos::protocol::TransactionSubmitResult::Ptr> submitTransaction(
        bcos::protocol::Transaction::Ptr transaction)
    {
        std::cout << "start resume at " << std::this_thread::get_id() << std::endl;

        auto result = std::make_shared<bcostars::protocol::TransactionSubmitResultImpl>(nullptr);
        bcostars::TransactionReceipt receipt;
        receipt.data.status = 79;
        auto receiptObj = std::make_shared<bcostars::protocol::TransactionReceiptImpl>(
            nullptr, [receipt = std::move(receipt)]() mutable { return &receipt; });
        result->setTransactionReceipt(receiptObj);
        std::cout << "resume ended " << std::this_thread::get_id() << std::endl;

        co_return result;
    }
};

struct TransactionPoolFixture
{
};

BOOST_FIXTURE_TEST_SUITE(TransactionPoolTest, TransactionPoolFixture)

BOOST_AUTO_TEST_CASE(mtTxPool)
{
    MockTransactionPoolMT<false> mock1;
    bcos::transaction_pool::TransactionPoolImpl transactionPool(nullptr, mock1);

    bcostars::Transaction transaction;
    bcostars::TransactionReceipt receipt;

    std::cout << "submitTransaction start at " << std::this_thread::get_id() << std::endl;
    bcos::task::syncWait(transactionPool.submitTransaction(transaction, receipt));
    std::cout << "submitTransaction success at " << std::this_thread::get_id() << std::endl;
    BOOST_CHECK_EQUAL(receipt.data.blockNumber, 10086);
    BOOST_CHECK_EQUAL(receipt.data.status, 100);

    MockTransactionPoolMT<true> mock2;
    bcos::transaction_pool::TransactionPoolImpl transactionPool2(nullptr, mock2);
    BOOST_CHECK_THROW(
        bcos::task::syncWait(transactionPool2.submitTransaction(transaction, receipt)),
        bcos::Error);
}

BOOST_AUTO_TEST_CASE(stTxPool)
{
    bcostars::Transaction transaction;
    bcostars::TransactionReceipt receipt;

    MockTransactionPoolST mock3;
    bcos::transaction_pool::TransactionPoolImpl transactionPool3(nullptr, mock3);
    bcos::task::syncWait(transactionPool3.submitTransaction(transaction, receipt));

    BOOST_CHECK_EQUAL(receipt.data.status, 79);
}

bcos::task::Task<void> mainTask()
{
    bcostars::Transaction transaction;
    bcostars::TransactionReceipt receipt;

    MockTransactionPoolMT<true> mock2;
    bcos::transaction_pool::TransactionPoolImpl transactionPool2(nullptr, mock2);
    BOOST_CHECK_THROW(co_await transactionPool2.submitTransaction(transaction, receipt),
        boost::wrapexcept<bcos::Error>);
}

BOOST_AUTO_TEST_CASE(multiCoro)
{
    bcos::task::syncWait(mainTask());
}

BOOST_AUTO_TEST_SUITE_END()