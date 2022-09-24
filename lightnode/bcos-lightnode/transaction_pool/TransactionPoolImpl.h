#pragma once

#include "../Log.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include "bcos-tars-protocol/tars/TransactionReceipt.h"
#include <bcos-concepts/Task.h>
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
    task::Task<void> impl_submitTransaction(
        bcos::concepts::transaction::Transaction auto transaction,
        bcos::concepts::receipt::TransactionReceipt auto& receipt)
    {
        TRANSACTIONPOOL_LOG(INFO) << "Submit transaction request";

        auto transactionData = std::make_shared<bcos::bytes>();
        bcos::concepts::serialize::encode(transaction, *transactionData);

        struct Awaitable : public CO_STD::suspend_always
        {
            Awaitable(decltype(transactionData)& transactionData,
                TransactionPoolType& transactionPool,
                std::remove_cvref_t<decltype(receipt)>& receipt)
              : m_transactionData(transactionData),
                m_transactionPool(transactionPool),
                m_receipt(receipt)
            {}

            constexpr void await_suspend(
                CO_STD::coroutine_handle<task::Task<void>::promise_type> handle)
            {
                bcos::concepts::getRef(m_transactionPool)
                    .asyncSubmit(std::move(m_transactionData),
                        [this, handle = std::move(handle)](Error::Ptr error,
                            bcos::protocol::TransactionSubmitResult::Ptr
                                transactionSubmitResult) mutable {
                            m_error = std::move(error);
                            if (transactionSubmitResult &&
                                transactionSubmitResult->transactionReceipt())
                            {
                                auto receiptImpl = std::dynamic_pointer_cast<
                                    bcostars::protocol::TransactionReceiptImpl>(
                                    transactionSubmitResult->transactionReceipt());
                                m_receipt = std::move(const_cast<bcostars::TransactionReceipt&>(
                                    receiptImpl->inner()));
                                m_withReceipt = true;
                            }

                            handle.resume();
                        });
            }


            decltype(transactionData)& m_transactionData;
            TransactionPoolType& m_transactionPool;
            std::remove_cvref_t<decltype(receipt)>& m_receipt;
            Error::Ptr m_error;
            bool m_withReceipt{};
        };

        Awaitable awaitable(transactionData, m_transactionPool, receipt);
        co_await awaitable;

        auto& error = awaitable.m_error;
        if (error)
        {
            TRANSACTIONPOOL_LOG(ERROR)
                << "submitTransaction error! " << boost::diagnostic_information(*error);

            BOOST_THROW_EXCEPTION(*error);
        }

        TRANSACTIONPOOL_LOG(DEBUG) << "Submit transaction successed";
    }

    TransactionPoolType m_transactionPool;
};
}  // namespace bcos::transaction_pool