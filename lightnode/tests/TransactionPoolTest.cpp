#include "bcos-tars-protocol/impl/TarsSerializable.h"

#include "bcos-tars-protocol/tars/Transaction.h"
#include "bcos-tars-protocol/tars/TransactionReceipt.h"
#include <bcos-lightnode/transaction_pool/TransactionPoolImpl.h>
#include <boost/test/unit_test.hpp>
#include <thread>

template <bool withError>
class MockTransactionPool
{
public:
    void asyncSubmit(bcos::bytesPointer, bcos::protocol::TxSubmitCallback callback)
    {
        // run in a new thread to simulate async call
        std::thread t([m_callback = std::move(callback)]() {
            std::cout << "start resume at " << std::this_thread::get_id() << std::endl;
            if constexpr (withError)
            {
                auto error = std::make_shared<bcos::Error>(-1, "mock error!");
                m_callback(std::move(error), nullptr);
            }
            else
            {
                m_callback(nullptr, nullptr);
            }
            std::cout << "resume ended " << std::this_thread::get_id() << std::endl;
        });
        t.join();
    }
};

struct TransactionPoolFixture
{
};

BOOST_FIXTURE_TEST_SUITE(TransactionPoolTest, TransactionPoolFixture)

BOOST_AUTO_TEST_CASE(testTransactionPool)
{
    MockTransactionPool<false> mock1;
    bcos::transaction_pool::TransactionPoolImpl transactionPool(mock1);

    bcostars::Transaction transaction;
    bcostars::TransactionReceipt receipt;

    std::cout << "submitTransaction start at " << std::this_thread::get_id() << std::endl;
    transactionPool.submitTransaction(transaction, receipt);
    std::cout << "submitTransaction success at " << std::this_thread::get_id() << std::endl;

    MockTransactionPool<true> mock2;
    bcos::transaction_pool::TransactionPoolImpl transactionPool2(mock2);
    BOOST_CHECK_THROW(transactionPool2.submitTransaction(transaction, receipt), bcos::Error);
}

BOOST_AUTO_TEST_SUITE_END()