//
// Created by Jimmy Shi on 2022/6/11.
//
#pragma once
#include "DuplicateTransactionFactory.h"
#include "JsonRpcImpl_2_0.h"

namespace bcos
{
namespace rpc
{
class DupTestTxJsonRpcImpl_2_0 : public JsonRpcImpl_2_0
{
public:
    using Ptr = std::shared_ptr<DupTestTxJsonRpcImpl_2_0>;
    DupTestTxJsonRpcImpl_2_0(GroupManager::Ptr _groupManager,
        bcos::gateway::GatewayInterface::Ptr _gatewayInterface,
        std::shared_ptr<boostssl::ws::WsService> _wsService)
      : JsonRpcImpl_2_0(_groupManager, _gatewayInterface, _wsService)
    {}

    // duplicate many tx to txpool
    void sendTransaction(std::string_view _groupID, std::string_view _nodeName,
        std::string_view _data, bool _requireProof, RespFunc _respFunc) override
    {
        // send directly
        JsonRpcImpl_2_0::sendTransaction(
            _groupID, _nodeName, _data, _requireProof, std::move(_respFunc));
        // duplicate many tx into txpool

        auto transactionDataPtr = decodeData(_data);
        auto nodeService = getNodeService(_groupID, _nodeName, "sendTransaction");
        auto txpool = nodeService->txpool();
        checkService(txpool, "txpool");

        // Note: avoid call tx->sender() or tx->verify() here in case of verify the same transaction
        // more than once
        auto tx = nodeService->blockFactory()->transactionFactory()->createTransaction(
            *transactionDataPtr, false);


        if (tx->to().empty())
        {
            // ignore deploy tx
            return;
        }

        RPC_IMPL_LOG(TRACE) << LOG_DESC("duplicate sendTransaction") << LOG_KV("group", _groupID)
                            << LOG_KV("node", _nodeName);

        auto submitCallback =
            [_groupID, _requireProof](Error::Ptr _error,
                bcos::protocol::TransactionSubmitResult::Ptr _transactionSubmitResult) {
                if (_error && _error->errorCode() != bcos::protocol::CommonError::SUCCESS)
                {
                    RPC_IMPL_LOG(ERROR)
                        << LOG_BADGE("sendTransaction") << LOG_KV("requireProof", _requireProof)
                        << LOG_KV("hash", _transactionSubmitResult ?
                                              _transactionSubmitResult->txHash().abridged() :
                                              "unknown")
                        << LOG_KV("code", _error->errorCode())
                        << LOG_KV("message", _error->errorMessage());


                    return;
                }
            };

        // generate account
        bcos::crypto::KeyPairInterface::Ptr keyPair =
            nodeService->blockFactory()->cryptoSuite()->signatureImpl()->generateKeyPair();

        // send many tx to txpool
        int64_t dup = 49;  // 1 tx can generate 49 + 1 tx(include original tx)
        DuplicateTransactionFactory::asyncMultiBuild(
            nodeService->blockFactory()->transactionFactory(), *transactionDataPtr, keyPair, dup,
            [submitCallback = std::move(submitCallback), txpool](
                bcos::protocol::Transaction::Ptr tx) {
                // std::cout << "sendtx: " << tx->nonce() << std::endl;
                auto encodedData = tx->encode();
                auto txData = std::make_shared<bytes>(encodedData.begin(), encodedData.end());
                txpool->asyncSubmit(txData, submitCallback);
            });
    }

    ~DupTestTxJsonRpcImpl_2_0() {}
};


}  // namespace rpc
}  // namespace bcos
