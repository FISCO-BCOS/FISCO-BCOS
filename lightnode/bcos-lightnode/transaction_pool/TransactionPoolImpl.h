#pragma once

#include "../Log.h"
#include "bcos-crypto/interfaces/crypto/CryptoSuite.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include "bcos-tars-protocol/tars/TransactionReceipt.h"
#include <bcos-concepts/transaction_pool/TransactionPool.h>
#include <bcos-framework/protocol/TransactionSubmitResult.h>
#include <bcos-task/Task.h>
#include <bcos-task/Wait.h>
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
    TransactionPoolImpl(
        bcos::crypto::CryptoSuite::Ptr cryptoSuite, TransactionPoolType transactionPool)
      : m_cryptoSuite(std::move(cryptoSuite)), m_transactionPool(std::move(transactionPool))
    {}

private:
    task::Task<void> impl_submitTransaction(
        bcos::concepts::transaction::Transaction auto transaction,
        bcos::concepts::receipt::TransactionReceipt auto& receipt)
    {
        TRANSACTIONPOOL_LOG(INFO) << "Submit transaction request";

        auto transactionImpl = std::make_shared<bcostars::protocol::TransactionImpl>(m_cryptoSuite,
            [m_transaction = std::move(transaction)]() mutable { return &m_transaction; });

        auto submitResult = co_await concepts::getRef(m_transactionPool)
                                .submitTransaction(std::move(transactionImpl));

        if (submitResult && submitResult->transactionReceipt())
        {
            auto receiptImpl =
                std::dynamic_pointer_cast<bcostars::protocol::TransactionReceiptImpl>(
                    submitResult->transactionReceipt());
            receipt = std::move(const_cast<bcostars::TransactionReceipt&>(receiptImpl->inner()));
        }

        TRANSACTIONPOOL_LOG(INFO) << "Submit transaction successed";
    }

    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    TransactionPoolType m_transactionPool;
};
}  // namespace bcos::transaction_pool