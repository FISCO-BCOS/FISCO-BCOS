#pragma once

#include "../Log.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include "bcos-tars-protocol/tars/TransactionReceipt.h"
#include <bcos-concepts/transaction_pool/TransactionPool.h>
#include <bcos-framework/protocol/TransactionSubmitResult.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <future>
#include <memory>


namespace bcos::transaction_pool
{
template <class TransactionPoolType>
class TransactionPoolImpl : public bcos::concepts::transacton_pool::TransactionPoolBase<
                                TransactionPoolImpl<TransactionPoolType>>
{
    friend bcos::concepts::transacton_pool::TransactionPoolBase<
        TransactionPoolImpl<TransactionPoolType>>;

public:
    TransactionPoolImpl(TransactionPoolType transactionPool)
      : m_transactionPool(std::move(transactionPool))
    {}

private:
    void impl_submitTransaction(bcos::concepts::transaction::Transaction auto transaction,
        bcos::concepts::receipt::TransactionReceipt auto& receipt)
    {
        TRANSACTIONPOOL_LOG(INFO) << "Submit transaction request";

        auto transactionData = std::make_shared<bcos::bytes>();
        bcos::concepts::serialize::encode(transaction, *transactionData);

        bool withReceipt = false;
        // must ensure the lifetime of promise
        auto promise = std::make_shared<std::promise<Error::Ptr>>();
        // Note: can't call get_future after called get()
        auto future = promise->get_future();
        transactionPool().asyncSubmit(std::move(transactionData),
            [promise, &receipt, &withReceipt](Error::Ptr error,
                bcos::protocol::TransactionSubmitResult::Ptr transactionSubmitResult) mutable {
                try
                {
                    if (transactionSubmitResult && transactionSubmitResult->transactionReceipt())
                    {
                        auto receiptImpl =
                            std::dynamic_pointer_cast<bcostars::protocol::TransactionReceiptImpl>(
                                transactionSubmitResult->transactionReceipt());
                        receipt = std::move(
                            const_cast<bcostars::TransactionReceipt&>(receiptImpl->inner()));
                        withReceipt = true;
                    }
                    // Note: promise set_value can't been called multiple times, otherwise will
                    // throw future_error exception
                    promise->set_value(std::move(error));
                }
                catch (std::future_error const& e)
                {
                    TRANSACTIONPOOL_LOG(INFO)
                        << LOG_DESC("asyncSubmit: the callback has already been called for timeout")
                        << LOG_KV("msg", boost::diagnostic_information(e));
                }
            });
        if (future.wait_for(std::chrono::seconds(c_sumitTransactionTimeout)) ==
            std::future_status::timeout)
        {
            auto errorMsg = "submitTransaction timeout";
            TRANSACTIONPOOL_LOG(ERROR) << errorMsg;
            auto error = std::make_shared<Error>(-1, errorMsg);
            BOOST_THROW_EXCEPTION(*error);
        }
        auto error = future.get();
        if (error)
        {
            TRANSACTIONPOOL_LOG(ERROR)
                << "submitTransaction error! " << boost::diagnostic_information(*error);

            BOOST_THROW_EXCEPTION(*error);
        }
    }

    auto& transactionPool() { return bcos::concepts::getRef(m_transactionPool); }

    TransactionPoolType m_transactionPool;

    int c_sumitTransactionTimeout = 30;
};
}  // namespace bcos::transaction_pool