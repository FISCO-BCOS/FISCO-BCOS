#pragma once

#include <bcos-tars-protocol/impl/TarsSerializable.h>

#include "P2PClientImpl.h"
#include <bcos-concepts/transaction_pool/TransactionPool.h>
#include <bcos-framework/protocol/TransactionSubmitResult.h>
#include <bcos-tars-protocol/tars/LightNode.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/throw_exception.hpp>
#include <future>

namespace bcos::transaction_pool
{

class TransactionPoolClientImpl
  : public bcos::concepts::transacton_pool::TransactionPoolBase<TransactionPoolClientImpl>
{
    friend bcos::concepts::transacton_pool::TransactionPoolBase<TransactionPoolClientImpl>;

public:
    TransactionPoolClientImpl(std::shared_ptr<p2p::P2PClientImpl> p2p) : m_p2p(std::move(p2p)) {}

private:
    auto& p2p() { return bcos::concepts::getRef(m_p2p); }

    void impl_submitTransaction(bcos::concepts::transaction::Transaction auto transaction,
        bcos::concepts::receipt::TransactionReceipt auto& receipt)
    {
        bcostars::RequestSendTransaction request;
        request.transaction = std::move(transaction);
        bcos::bytes requestBuffer;
        bcos::concepts::serialize::encode(request, requestBuffer);

        auto nodeID = p2p().randomSelectNode();
        auto responseBuffer = p2p().sendMessageByNodeID(
            bcos::protocol::LIGHTNODE_SENDTRANSACTION, nodeID, bcos::ref(requestBuffer));

        bcostars::ResponseSendTransaction response;
        bcos::concepts::serialize::decode(responseBuffer, response);
        if (response.error.errorCode)
            BOOST_THROW_EXCEPTION(std::runtime_error(response.error.errorMessage));

        std::swap(response.receipt, receipt);
    }

    std::shared_ptr<p2p::P2PClientImpl> m_p2p;
};
}  // namespace bcos::transaction_pool