#include "TxPoolServiceServer.h"
#include "Common/TarsUtils.h"
using namespace bcostars;

bcostars::Error TxPoolServiceServer::asyncFillBlock(const vector<vector<tars::Char>>& txHashs,
    vector<bcostars::Transaction>& filled, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    auto hashList = std::make_shared<std::vector<bcos::crypto::HashType>>();
    for (auto const& hashData : txHashs)
    {
        hashList->push_back(bcos::crypto::HashType(
            reinterpret_cast<const bcos::byte*>(hashData.data()), hashData.size()));
    }

    m_txpoolInitializer->txpool()->asyncFillBlock(
        hashList, [current](bcos::Error::Ptr error, bcos::protocol::TransactionsPtr txs) {
            std::vector<bcostars::Transaction> txList;
            if (error)
            {
                async_response_asyncFillBlock(current, toTarsError(error), txList);
                TXPOOLSERVICE_LOG(WARNING)
                    << LOG_DESC("asyncFillBlock failed") << LOG_KV("code", error->errorCode())
                    << LOG_KV("msg", error->errorMessage());
                return;
            }
            for (auto tx : *txs)
            {
                txList.push_back(
                    std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx)->inner());
            }

            async_response_asyncFillBlock(current, toTarsError(error), txList);
        });

    return bcostars::Error();
}


bcostars::Error TxPoolServiceServer::asyncMarkTxs(const vector<vector<tars::Char>>& txHashs,
    tars::Bool sealedFlag, tars::Int64 _batchId, const vector<tars::Char>& _batchHash,
    tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    auto hashList = std::make_shared<std::vector<bcos::crypto::HashType>>();
    for (auto hashData : txHashs)
    {
        hashList->push_back(bcos::crypto::HashType(
            reinterpret_cast<const bcos::byte*>(hashData.data()), hashData.size()));
    }
    auto batchHash = bcos::crypto::HashType(
        reinterpret_cast<const bcos::byte*>(_batchHash.data()), _batchHash.size());
    m_txpoolInitializer->txpool()->asyncMarkTxs(
        hashList, sealedFlag, _batchId, batchHash, [current](bcos::Error::Ptr error) {
            async_response_asyncMarkTxs(current, toTarsError(error));
        });
    return bcostars::Error();
}

bcostars::Error TxPoolServiceServer::asyncNotifyBlockResult(tars::Int64 blockNumber,
    const vector<bcostars::TransactionSubmitResult>& result, tars::TarsCurrentPtr current)
{
    current->setResponse(false);

    auto bcosResultList = std::make_shared<bcos::protocol::TransactionSubmitResults>();
    for (auto tarsResult : result)
    {
        auto bcosResult = std::make_shared<bcostars::protocol::TransactionSubmitResultImpl>(
            m_txpoolInitializer->cryptoSuite(),
            [inner = std::move(const_cast<bcostars::TransactionSubmitResult&>(
                 tarsResult))]() mutable { return &inner; });
        bcosResultList->push_back(bcosResult);
    }

    m_txpoolInitializer->txpool()->asyncNotifyBlockResult(
        blockNumber, bcosResultList, [current](bcos::Error::Ptr error) {
            async_response_asyncNotifyBlockResult(current, toTarsError(error));
        });
    return bcostars::Error();
}

bcostars::Error TxPoolServiceServer::asyncNotifyTxsSyncMessage(const bcostars::Error& error,
    const std::string& id, const vector<tars::Char>& nodeID, const vector<tars::Char>& data,
    tars::TarsCurrentPtr current)
{
    current->setResponse(false);

    auto bcosNodeID = m_txpoolInitializer->cryptoSuite()->keyFactory()->createKey(
        bcos::bytes(nodeID.begin(), nodeID.end()));

    m_txpoolInitializer->txpool()->asyncNotifyTxsSyncMessage(toBcosError(error), id, bcosNodeID,
        bcos::bytesConstRef(reinterpret_cast<const bcos::byte*>(data.data()), data.size()),
        [current](bcos::Error::Ptr error) {
            async_response_asyncNotifyTxsSyncMessage(current, toTarsError(error));
        });

    return bcostars::Error();
}

bcostars::Error TxPoolServiceServer::asyncSealTxs(tars::Int64 txsLimit,
    const vector<vector<tars::Char>>& avoidTxs, bcostars::Block& txsList,
    bcostars::Block& sysTxsList, tars::TarsCurrentPtr current)
{
    current->setResponse(false);

    auto bcosAvoidTxs = std::make_shared<bcos::txpool::TxsHashSet>();
    for (auto tx : avoidTxs)
    {
        bcosAvoidTxs->insert(bcos::crypto::HashType(bcos::bytes(tx.begin(), tx.end())));
    }

    m_txpoolInitializer->txpool()->asyncSealTxs(txsLimit, bcosAvoidTxs,
        [current](bcos::Error::Ptr error, bcos::protocol::Block::Ptr _txsList,
            bcos::protocol::Block::Ptr _sysTxsList) {
            if (error)
            {
                TXPOOLSERVICE_LOG(WARNING)
                    << LOG_DESC("asyncSealTxs failed") << LOG_KV("code", error->errorCode())
                    << LOG_KV("msg", error->errorMessage());
                async_response_asyncSealTxs(
                    current, toTarsError(error), bcostars::Block(), bcostars::Block());
                return;
            }
            async_response_asyncSealTxs(current, toTarsError(error),
                std::dynamic_pointer_cast<bcostars::protocol::BlockImpl>(_txsList)->inner(),
                std::dynamic_pointer_cast<bcostars::protocol::BlockImpl>(_sysTxsList)->inner());
        });

    return bcostars::Error();
}

bcostars::Error TxPoolServiceServer::asyncSubmit(const vector<tars::Char>& tx,
    bcostars::TransactionSubmitResult& result, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    auto dataPtr = std::make_shared<bcos::bytes>(tx.begin(), tx.end());
    m_txpoolInitializer->txpool()->asyncSubmit(dataPtr,
        [current](bcos::Error::Ptr error, bcos::protocol::TransactionSubmitResult::Ptr result) {
            async_response_asyncSubmit(current, toTarsError(error),
                std::dynamic_pointer_cast<bcostars::protocol::TransactionSubmitResultImpl>(result)
                    ->inner());
        });

    return bcostars::Error();
}

bcostars::Error TxPoolServiceServer::asyncVerifyBlock(const vector<tars::Char>& generatedNodeID,
    const vector<tars::Char>& block, tars::Bool& result, tars::TarsCurrentPtr current)
{
    current->setResponse(false);

    bcos::crypto::PublicPtr pk = m_txpoolInitializer->cryptoSuite()->keyFactory()->createKey(
        bcos::bytesConstRef((const bcos::byte*)generatedNodeID.data(), generatedNodeID.size()));
    m_txpoolInitializer->txpool()->asyncVerifyBlock(pk,
        bcos::bytesConstRef((const bcos::byte*)block.data(), block.size()),
        [current](bcos::Error::Ptr error, bool result) {
            async_response_asyncVerifyBlock(current, toTarsError(error), result);
        });

    return bcostars::Error();
}

bcostars::Error TxPoolServiceServer::notifyConnectedNodes(
    const vector<vector<tars::Char>>& connectedNodes, tars::TarsCurrentPtr current)
{
    current->setResponse(false);

    bcos::crypto::NodeIDSet bcosNodeIDSet;
    for (auto const& it : connectedNodes)
    {
        bcosNodeIDSet.insert(m_txpoolInitializer->cryptoSuite()->keyFactory()->createKey(
            bcos::bytesConstRef((const bcos::byte*)it.data(), it.size())));
    }

    m_txpoolInitializer->txpool()->notifyConnectedNodes(
        bcosNodeIDSet, [current](bcos::Error::Ptr error) {
            async_response_notifyConnectedNodes(current, toTarsError(error));
        });

    return bcostars::Error();
}

bcostars::Error TxPoolServiceServer::notifyConsensusNodeList(
    const vector<bcostars::ConsensusNode>& consensusNodeList, tars::TarsCurrentPtr current)
{
    current->setResponse(false);

    bcos::consensus::ConsensusNodeList bcosNodeList;
    for (auto const& it : consensusNodeList)
    {
        auto node = std::make_shared<bcos::consensus::ConsensusNode>(
            m_txpoolInitializer->cryptoSuite()->keyFactory()->createKey(
                bcos::bytesConstRef((const bcos::byte*)it.nodeID.data(), it.nodeID.size())),
            it.weight);
        bcosNodeList.emplace_back(node);
    }

    m_txpoolInitializer->txpool()->notifyConsensusNodeList(
        bcosNodeList, [current](bcos::Error::Ptr error) {
            async_response_notifyConsensusNodeList(current, toTarsError(error));
        });

    return bcostars::Error();
}

bcostars::Error TxPoolServiceServer::notifyObserverNodeList(
    const vector<bcostars::ConsensusNode>& observerNodeList, tars::TarsCurrentPtr current)
{
    current->setResponse(false);

    bcos::consensus::ConsensusNodeList bcosObserverNodeList;
    for (auto const& it : observerNodeList)
    {
        auto node = std::make_shared<bcos::consensus::ConsensusNode>(
            m_txpoolInitializer->cryptoSuite()->keyFactory()->createKey(
                bcos::bytesConstRef((const bcos::byte*)it.nodeID.data(), it.nodeID.size())),
            it.weight);
        bcosObserverNodeList.emplace_back(node);
    }

    m_txpoolInitializer->txpool()->notifyObserverNodeList(
        bcosObserverNodeList, [current](bcos::Error::Ptr error) {
            async_response_notifyObserverNodeList(current, toTarsError(error));
        });

    return bcostars::Error();
}

bcostars::Error TxPoolServiceServer::asyncGetPendingTransactionSize(
    tars::Int64& _pendingTxsSize, tars::TarsCurrentPtr _current)
{
    _current->setResponse(false);
    m_txpoolInitializer->txpool()->asyncGetPendingTransactionSize(
        [_current](bcos::Error::Ptr _error, uint64_t _txsSize) {
            async_response_asyncGetPendingTransactionSize(_current, toTarsError(_error), _txsSize);
        });
    return bcostars::Error();
}

bcostars::Error TxPoolServiceServer::asyncResetTxPool(tars::TarsCurrentPtr _current)
{
    _current->setResponse(false);
    m_txpoolInitializer->txpool()->asyncResetTxPool([_current](bcos::Error::Ptr _error) {
        async_response_asyncResetTxPool(_current, toTarsError(_error));
    });
    return bcostars::Error();
}