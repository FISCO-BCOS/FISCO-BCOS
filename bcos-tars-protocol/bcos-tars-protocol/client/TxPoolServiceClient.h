#pragma once

#include "bcos-tars-protocol/ErrorConverter.h"
#include "bcos-tars-protocol/protocol/BlockImpl.h"
#include "bcos-tars-protocol/protocol/TransactionImpl.h"
#include "bcos-tars-protocol/protocol/TransactionSubmitResultImpl.h"
#include "bcos-tars-protocol/tars/Transaction.h"
#include "bcos-tars-protocol/tars/TransactionSubmitResult.h"
#include "bcos-tars-protocol/tars/TxPoolService.h"
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-framework/txpool/TxPoolInterface.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Common.h>
#include <boost/throw_exception.hpp>
#include <memory>
#include <utility>
#include <variant>

namespace bcostars
{
class TxPoolServiceClient : public bcos::txpool::TxPoolInterface
{
public:
    TxPoolServiceClient(bcostars::TxPoolServicePrx _proxy,
        bcos::crypto::CryptoSuite::Ptr _cryptoSuite,
        bcos::protocol::BlockFactory::Ptr _blockFactory);
    ~TxPoolServiceClient() override = default;

    bcos::task::Task<bcos::protocol::TransactionSubmitResult::Ptr> submitTransaction(
        bcos::protocol::Transaction::Ptr transaction, bool waitForReceipt) override;

    bcos::task::Task<void> broadcastTransaction(
        [[maybe_unused]] const bcos::protocol::Transaction& transaction) override;


    bcos::task::Task<void> broadcastTransactionBuffer(
        [[maybe_unused]] bcos::bytesConstRef _data) override;

    std::tuple<bcos::protocol::Block::Ptr, bcos::protocol::Block::Ptr> sealTxs(
        uint64_t _txsLimit, bcos::txpool::TxsHashSetPtr _avoidTxs) override;

    void asyncMarkTxs(const bcos::crypto::HashList& _txsHash, bool _sealedFlag,
        bcos::protocol::BlockNumber _batchId, bcos::crypto::HashType const& _batchHash,
        std::function<void(bcos::Error::Ptr)> _onRecvResponse) override;

    void asyncVerifyBlock(bcos::crypto::PublicPtr _generatedNodeID,
        bcos::bytesConstRef const& _block,
        std::function<void(bcos::Error::Ptr, bool)> _onVerifyFinished) override;

    void asyncFillBlock(bcos::crypto::HashListPtr _txsHash,
        std::function<void(bcos::Error::Ptr, bcos::protocol::ConstTransactionsPtr)> _onBlockFilled)
        override;

    void asyncNotifyBlockResult(bcos::protocol::BlockNumber _blockNumber,
        bcos::protocol::TransactionSubmitResultsPtr _txsResult,
        std::function<void(bcos::Error::Ptr)> _onNotifyFinished) override;

    void asyncNotifyTxsSyncMessage(bcos::Error::Ptr _error, std::string const& _id,
        bcos::crypto::NodeIDPtr _nodeID, bcos::bytesConstRef _data,
        std::function<void(bcos::Error::Ptr _error)> _onRecv) override;

    void notifyConnectedNodes(bcos::crypto::NodeIDSet const& _connectedNodes,
        std::function<void(bcos::Error::Ptr)> _onRecvResponse) override;

    void notifyConsensusNodeList(bcos::consensus::ConsensusNodeList const& _consensusNodeList,
        std::function<void(bcos::Error::Ptr)> _onRecvResponse) override;

    void notifyObserverNodeList(bcos::consensus::ConsensusNodeList const& _observerNodeList,
        std::function<void(bcos::Error::Ptr)> _onRecvResponse) override;

    // for RPC to get pending transactions
    void asyncGetPendingTransactionSize(
        std::function<void(bcos::Error::Ptr, uint64_t)> _onGetTxsSize) override;

    void asyncResetTxPool(std::function<void(bcos::Error::Ptr)> _onRecv) override;

protected:
    void start() override;
    void stop() override;

private:
    bcostars::TxPoolServicePrx m_proxy;
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
};

}  // namespace bcostars
