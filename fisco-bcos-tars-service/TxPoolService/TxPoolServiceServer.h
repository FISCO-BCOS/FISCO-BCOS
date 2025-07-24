#pragma once

#include "libinitializer/TxPoolInitializer.h"
#include <bcos-framework/consensus/ConsensusNode.h>
#include <bcos-tars-protocol/ErrorConverter.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionSubmitResultImpl.h>
#include <bcos-tars-protocol/tars/CommonProtocol.h>
#include <bcos-tars-protocol/tars/TxPoolService.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <servant/Servant.h>
#include <memory>

namespace bcostars
{
struct TxPoolServiceParam
{
    bcos::initializer::TxPoolInitializer::Ptr txPoolInitializer;
};
class TxPoolServiceServer : public bcostars::TxPoolService
{
public:
    using Ptr = std::shared_ptr<TxPoolServiceServer>;
    TxPoolServiceServer(TxPoolServiceParam const& _param)
      : m_txpoolInitializer(_param.txPoolInitializer)
    {}
    ~TxPoolServiceServer() override {}

    void initialize() override {}
    void destroy() override {}

    bcostars::Error submit(const bcostars::Transaction& tx,
        bcostars::TransactionSubmitResult& result, tars::TarsCurrentPtr current) override;

    bcostars::Error broadcastTransaction(
        const bcostars::Transaction& tx, tars::TarsCurrentPtr current) override;

    bcostars::Error broadcastTransactionBuffer(
        const vector<tars::Char>& transactionBuffer, tars::TarsCurrentPtr current) override;

    bcostars::Error asyncFillBlock(const vector<vector<tars::Char>>& txHashs,
        vector<bcostars::Transaction>& filled, tars::TarsCurrentPtr current) override;

    bcostars::Error asyncMarkTxs(const vector<vector<tars::Char>>& txHashs, tars::Bool sealedFlag,
        tars::Int64 _batchId, const vector<tars::Char>& _batchHash,
        tars::TarsCurrentPtr current) override;

    bcostars::Error asyncNotifyBlockResult(tars::Int64 blockNumber,
        const vector<bcostars::TransactionSubmitResult>& result,
        tars::TarsCurrentPtr current) override;

    bcostars::Error asyncNotifyTxsSyncMessage(const bcostars::Error& error, const std::string& id,
        const vector<tars::Char>& nodeID, const vector<tars::Char>& data,
        tars::TarsCurrentPtr current) override;

    bcostars::Error asyncSealTxs(tars::Int64 txsLimit, const vector<vector<tars::Char>>& avoidTxs,
        bcostars::Block& txsList, bcostars::Block& sysTxsList,
        tars::TarsCurrentPtr current) override;

    bcostars::Error asyncVerifyBlock(const vector<tars::Char>& generatedNodeID,
        const bcostars::Block& block, tars::Bool& result, tars::TarsCurrentPtr current) override;

    bcostars::Error notifyConnectedNodes(
        const vector<vector<tars::Char>>& connectedNodes, tars::TarsCurrentPtr current) override;

    bcostars::Error notifyConsensusNodeList(
        const vector<bcostars::ConsensusNode>& consensusNodeList,
        tars::TarsCurrentPtr current) override;

    bcostars::Error notifyObserverNodeList(const vector<bcostars::ConsensusNode>& observerNodeList,
        tars::TarsCurrentPtr current) override;
    bcostars::Error asyncGetPendingTransactionSize(
        tars::Int64& _pendingTxsSize, tars::TarsCurrentPtr _current) override;
    bcostars::Error asyncResetTxPool(tars::TarsCurrentPtr _current) override;

private:
    bcos::initializer::TxPoolInitializer::Ptr m_txpoolInitializer;
};
}  // namespace bcostars