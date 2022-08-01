#pragma once

#include "../Log.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include "bcos-tars-protocol/tars/TransactionReceipt.h"
#include <bcos-concepts/transaction_pool/TransactionPool.h>
#include <bcos-framework/protocol/TransactionSubmitResult.h>
#include <bcos-utilities/FixedBytes.h>
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

        std::promise<Error::Ptr> promise;
        transactionPool().asyncSubmit(std::move(transactionData),
            [&promise, &receipt](Error::Ptr error,
                bcos::protocol::TransactionSubmitResult::Ptr transactionSubmitResult) mutable {
                if (!error)
                {
                    auto receiptImpl =
                        std::dynamic_pointer_cast<bcostars::protocol::TransactionReceiptImpl>(
                            transactionSubmitResult->transactionReceipt());
                    receipt =
                        std::move(const_cast<bcostars::TransactionReceipt&>(receiptImpl->inner()));
                }

                promise.set_value(std::move(error));
            });

        auto error = promise.get_future().get();
        if (error)
            BOOST_THROW_EXCEPTION(*error);
    }

    auto& transactionPool() { return bcos::concepts::getRef(m_transactionPool); }

    TransactionPoolType m_transactionPool;
};
}  // namespace bcos::transaction_pool