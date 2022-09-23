#include <bcos-tars-protocol/impl/TarsSerializable.h>

#include <bcos-lightnode/transaction_pool/TransactionPoolImpl.h>
#include <bcos-tars-protocol/protocol/TransactionSubmitResultImpl.h>
#include <bcos-tars-protocol/tars/Transaction.h>
#include <bcos-tars-protocol/tars/TransactionReceipt.h>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>
#include <thread>

template <bool withError>
class MockTransactionPoolMT
{
public:
    void asyncSubmit(bcos::bytesPointer, bcos::protocol::TxSubmitCallback callback)
    {
        // run in a new thread to simulate async call
        std::thread t([m_callback = std::move(callback)]() {
            std::cout << "start resume at " << std::this_thread::get_id() << std::endl;
            if constexpr (withError)
            {
                try
                {
                    auto error = std::make_shared<bcos::Error>(-1, "mock error!");
                    m_callback(std::move(error), nullptr);
                }
                catch (bcos::Error& error)
                {
                    std::cout << "Exception successed throwd!" << std::endl;
                    return;
                }

                BOOST_FAIL("No exception throw!");
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
                m_callback(nullptr, result);
            }
            std::cout << "resume ended " << std::this_thread::get_id() << std::endl;
        });
        t.join();
    }
};

class MockTransactionPoolST
{
public:
    void asyncSubmit(bcos::bytesPointer, bcos::protocol::TxSubmitCallback callback)
    {
        std::cout << "start resume at " << std::this_thread::get_id() << std::endl;

        auto result = std::make_shared<bcostars::protocol::TransactionSubmitResultImpl>(nullptr);
        bcostars::TransactionReceipt receipt;
        receipt.data.status = 79;
        auto receiptObj = std::make_shared<bcostars::protocol::TransactionReceiptImpl>(
            nullptr, [receipt = std::move(receipt)]() mutable { return &receipt; });
        result->setTransactionReceipt(receiptObj);
        callback(nullptr, result);
        std::cout << "resume ended " << std::this_thread::get_id() << std::endl;
    }
};

struct TransactionPoolFixture
{
};

BOOST_FIXTURE_TEST_SUITE(TransactionPoolTest, TransactionPoolFixture)

BOOST_AUTO_TEST_CASE(mtTxPool)
{
    MockTransactionPoolMT<false> mock1;
    bcos::transaction_pool::TransactionPoolImpl transactionPool(mock1);

    bcostars::Transaction transaction;
    bcostars::TransactionReceipt receipt;

    std::cout << "submitTransaction start at " << std::this_thread::get_id() << std::endl;
    transactionPool.submitTransaction(transaction, receipt);
    std::cout << "submitTransaction success at " << std::this_thread::get_id() << std::endl;
    BOOST_CHECK_EQUAL(receipt.data.blockNumber, 10086);
    BOOST_CHECK_EQUAL(receipt.data.status, 100);

    MockTransactionPoolMT<true> mock2;
    bcos::transaction_pool::TransactionPoolImpl transactionPool2(mock2);
    transactionPool2.submitTransaction(transaction, receipt);
}

BOOST_AUTO_TEST_CASE(stTxPool)
{
    bcostars::Transaction transaction;
    bcostars::TransactionReceipt receipt;

    MockTransactionPoolST mock3;
    bcos::transaction_pool::TransactionPoolImpl transactionPool3(mock3);
    transactionPool3.submitTransaction(transaction, receipt);

    BOOST_CHECK_EQUAL(receipt.data.status, 79);
}

BOOST_AUTO_TEST_SUITE_END()