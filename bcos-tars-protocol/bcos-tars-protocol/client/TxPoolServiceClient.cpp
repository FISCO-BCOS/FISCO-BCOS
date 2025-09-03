#include "TxPoolServiceClient.h"

#include <memory>
#include <utility>

bcostars::TxPoolServiceClient::TxPoolServiceClient(bcostars::TxPoolServicePrx _proxy,
    bcos::crypto::CryptoSuite::Ptr _cryptoSuite, bcos::protocol::BlockFactory::Ptr _blockFactory)
  : m_proxy(_proxy), m_cryptoSuite(_cryptoSuite), m_blockFactory(_blockFactory)
{}
bcos::task::Task<bcos::protocol::TransactionSubmitResult::Ptr>
bcostars::TxPoolServiceClient::submitTransaction(
    bcos::protocol::Transaction::Ptr transaction, bool waitForReceipt)
{
    struct TarsCallback : public bcostars::TxPoolServicePrxCallback
    {
        void callback_submit(
            const bcostars::Error& ret, const bcostars::TransactionSubmitResult& result) override
        {
            if (ret.errorCode != 0)
            {
                m_submitResult = toBcosError(ret);
                m_handle.resume();
                return;
            }

            auto bcosResult = std::make_shared<bcostars::protocol::TransactionSubmitResultImpl>(
                [inner = std::move(const_cast<bcostars::TransactionSubmitResult&>(
                     result))]() mutable { return &inner; });
            m_submitResult = std::move(bcosResult);
            m_handle.resume();
        }
        void callback_submit_exception(tars::Int32 ret) override
        {
            m_submitResult = toBcosError(ret);
            m_handle.resume();
        }

        std::coroutine_handle<> m_handle;
        std::variant<std::monostate, bcos::Error::Ptr,
            bcostars::protocol::TransactionSubmitResultImpl::Ptr>
            m_submitResult;
        bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    };

    struct Awaitable
    {
        constexpr bool await_ready() { return false; }
        void await_suspend(std::coroutine_handle<> handle)
        {
            m_callback->m_handle = handle;
            m_proxy->tars_set_timeout(600000)->async_submit(m_callback,
                dynamic_cast<bcostars::protocol::TransactionImpl&>(*m_transaction)
                    .inner());  // tars take the m_callback ownership
        }
        bcostars::protocol::TransactionSubmitResultImpl::Ptr await_resume()
        {
            if (std::holds_alternative<bcos::Error::Ptr>(m_callback->m_submitResult))
            {
                BOOST_THROW_EXCEPTION(*std::get<bcos::Error::Ptr>(m_callback->m_submitResult));
            }

            if (!std::holds_alternative<bcostars::protocol::TransactionSubmitResultImpl::Ptr>(
                    m_callback->m_submitResult))
            {
                BOOST_THROW_EXCEPTION(BCOS_ERROR(-1, "No value!"));
            }

            return std::move(std::get<bcostars::protocol::TransactionSubmitResultImpl::Ptr>(
                m_callback->m_submitResult));
        }

        TarsCallback* m_callback = nullptr;
        bcos::protocol::Transaction::Ptr m_transaction;
        bcostars::TxPoolServicePrx m_proxy;
        bool m_waitForReceipt = false;
    };

    auto tarsCallback = std::make_unique<TarsCallback>();
    tarsCallback->m_cryptoSuite = m_cryptoSuite;

    Awaitable awaitable{
        .m_callback = tarsCallback.release(),
        .m_transaction = std::move(transaction),
        .m_proxy = m_proxy,
    };

    co_return co_await awaitable;
}
bcos::task::Task<void> bcostars::TxPoolServiceClient::broadcastTransaction(
    [[maybe_unused]] const bcos::protocol::Transaction& transaction)
{
    struct TarsCallback : public bcostars::TxPoolServicePrxCallback
    {
        void callback_broadcastTransaction(const bcostars::Error& ret) override
        {
            m_error = toBcosError(ret);
        }
        void callback_broadcastTransaction_exception(tars::Int32 ret) override
        {
            m_error = toBcosError(ret);
        }

        bcos::Error::Ptr m_error;
    };

    auto tarsCallback = std::make_unique<TarsCallback>();
    m_proxy->tars_set_timeout(600000)->async_broadcastTransaction(tarsCallback.release(),
        dynamic_cast<const bcostars::protocol::TransactionImpl&>(transaction)
            .inner());  // tars take the m_callback ownership
    co_return;
}
bcos::task::Task<void> bcostars::TxPoolServiceClient::broadcastTransactionBuffer(
    [[maybe_unused]] bcos::bytesConstRef _data)
{
    struct TarsCallback : public bcostars::TxPoolServicePrxCallback
    {
        void callback_broadcastTransactionBuffer(const bcostars::Error& ret) override
        {
            auto error = toBcosError(ret);
            if (error)
            {
                BOOST_THROW_EXCEPTION(*error);
            }
        }
        void callback_broadcastTransactionBuffer_exception(tars::Int32 ret) override
        {
            auto error = toBcosError(ret);
            if (error)
            {
                BOOST_THROW_EXCEPTION(*error);
            }
        }
    };

    auto tarsCallback = std::make_unique<TarsCallback>();

    m_proxy->tars_set_timeout(600000)->async_broadcastTransactionBuffer(tarsCallback.release(),
        std::vector<char>(_data.begin(), _data.end()));  // tars take the m_callback ownership
    co_return;
}
std::tuple<bcos::protocol::Block::Ptr, bcos::protocol::Block::Ptr>
bcostars::TxPoolServiceClient::sealTxs(uint64_t _txsLimit)
{
    vector<vector<tars::Char>> tarsAvoidTxs;
    auto txs = std::make_shared<bcostars::protocol::BlockImpl>();
    auto sysTxs = std::make_shared<bcostars::protocol::BlockImpl>();
    m_proxy->asyncSealTxs(_txsLimit, tarsAvoidTxs, txs->inner(), sysTxs->inner());
    return {txs, sysTxs};
}
void bcostars::TxPoolServiceClient::asyncMarkTxs(const bcos::crypto::HashList& _txsHash,
    bool _sealedFlag, bcos::protocol::BlockNumber _batchId,
    bcos::crypto::HashType const& _batchHash, std::function<void(bcos::Error::Ptr)> _onRecvResponse)
{
    class Callback : public bcostars::TxPoolServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr)> callback) : m_callback(std::move(callback))
        {}

        void callback_asyncMarkTxs(const bcostars::Error& ret) override
        {
            m_callback(toBcosError(ret));
        }

        void callback_asyncMarkTxs_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret));
        }

    private:
        std::function<void(bcos::Error::Ptr)> m_callback;
    };

    vector<vector<tars::Char>> txHashList;
    for (auto& it : _txsHash)
    {
        txHashList.push_back(std::vector<char>(it.begin(), it.end()));
    }

    m_proxy->async_asyncMarkTxs(new Callback(_onRecvResponse), txHashList, _sealedFlag, _batchId,
        std::vector<char>(_batchHash.begin(), _batchHash.end()));
}
void bcostars::TxPoolServiceClient::asyncVerifyBlock(bcos::crypto::PublicPtr _generatedNodeID,
    bcos::protocol::Block::ConstPtr _block,
    std::function<void(bcos::Error::Ptr, bool)> _onVerifyFinished)
{
    class Callback : public bcostars::TxPoolServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr, bool)> callback)
          : m_callback(std::move(callback))
        {}

        void callback_asyncVerifyBlock(const bcostars::Error& ret, tars::Bool result) override
        {
            m_callback(toBcosError(ret), result);
        }

        void callback_asyncVerifyBlock_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret), false);
        }

    private:
        std::function<void(bcos::Error::Ptr, bool)> m_callback;
    };

    const auto& blockImpl = dynamic_cast<const bcostars::protocol::BlockImpl&>(*_block);
    auto nodeID = _generatedNodeID->data();
    m_proxy->async_asyncVerifyBlock(new Callback(_onVerifyFinished),
        std::vector<char>(nodeID.begin(), nodeID.end()), blockImpl.inner());
}
void bcostars::TxPoolServiceClient::asyncFillBlock(bcos::crypto::HashListPtr _txsHash,
    std::function<void(bcos::Error::Ptr, bcos::protocol::ConstTransactionsPtr)> _onBlockFilled)

{
    class Callback : public bcostars::TxPoolServicePrxCallback
    {
    public:
        Callback(
            std::function<void(bcos::Error::Ptr, bcos::protocol::ConstTransactionsPtr)> callback,
            bcos::crypto::CryptoSuite::Ptr cryptoSuite)
          : m_callback(callback), m_cryptoSuite(cryptoSuite)
        {}

        void callback_asyncFillBlock(
            const bcostars::Error& ret, const vector<bcostars::Transaction>& filled) override
        {
            auto mutableFilled = const_cast<vector<bcostars::Transaction>*>(&filled);
            auto txs = std::make_shared<bcos::protocol::ConstTransactions>();
            for (auto&& it : *mutableFilled)
            {
                auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
                    [m_tx = std::move(it)]() mutable { return &m_tx; });
                txs->push_back(tx);
            }
            m_callback(toBcosError(ret), txs);
        }

        void callback_asyncFillBlock_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret), nullptr);
        }

    private:
        std::function<void(bcos::Error::Ptr, bcos::protocol::ConstTransactionsPtr)> m_callback;
        bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    };

    vector<vector<tars::Char>> hashList;
    for (auto hashData : *_txsHash)
    {
        hashList.emplace_back(hashData.begin(), hashData.end());
    }

    m_proxy->async_asyncFillBlock(new Callback(_onBlockFilled, m_cryptoSuite), hashList);
}
void bcostars::TxPoolServiceClient::asyncNotifyBlockResult(bcos::protocol::BlockNumber _blockNumber,
    bcos::protocol::TransactionSubmitResultsPtr _txsResult,
    std::function<void(bcos::Error::Ptr)> _onNotifyFinished)
{
    class Callback : public bcostars::TxPoolServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr)> callback) : m_callback(callback) {}

        void callback_asyncNotifyBlockResult(const bcostars::Error& ret) override
        {
            m_callback(toBcosError(ret));
        }

        void callback_asyncNotifyBlockResult_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret));
        }

    private:
        std::function<void(bcos::Error::Ptr)> m_callback;
    };

    vector<bcostars::TransactionSubmitResult> resultList;
    for (auto& it : *_txsResult)
    {
        resultList.emplace_back(
            std::dynamic_pointer_cast<bcostars::protocol::TransactionSubmitResultImpl>(it)
                ->inner());
    }

    m_proxy->async_asyncNotifyBlockResult(
        new Callback(_onNotifyFinished), _blockNumber, resultList);
}
void bcostars::TxPoolServiceClient::asyncNotifyTxsSyncMessage(bcos::Error::Ptr _error,
    std::string const& _id, bcos::crypto::NodeIDPtr _nodeID, bcos::bytesConstRef _data,
    std::function<void(bcos::Error::Ptr _error)> _onRecv)
{
    class Callback : public bcostars::TxPoolServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr _error)> callback) : m_callback(callback) {}

        void callback_asyncNotifyTxsSyncMessage(const bcostars::Error& ret) override
        {
            m_callback(toBcosError(ret));
        }

        void callback_asyncNotifyTxsSyncMessage_exception(tars::Int32 ret) override
        {
            bcos::bytes empty;
            m_callback(toBcosError(ret));
        }

    private:
        std::function<void(bcos::Error::Ptr _error)> m_callback;
    };

    auto nodeID = _nodeID->data();
    m_proxy->async_asyncNotifyTxsSyncMessage(new Callback(_onRecv), toTarsError(_error), _id,
        std::vector<char>(nodeID.begin(), nodeID.end()),
        std::vector<char>(_data.begin(), _data.end()));
}
void bcostars::TxPoolServiceClient::notifyConnectedNodes(
    bcos::crypto::NodeIDSet const& _connectedNodes,
    std::function<void(bcos::Error::Ptr)> _onRecvResponse)
{
    class Callback : public bcostars::TxPoolServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr _error)> callback) : m_callback(callback) {}

        void callback_notifyConnectedNodes(const bcostars::Error& ret) override
        {
            m_callback(toBcosError(ret));
        }

        void callback_notifyConnectedNodes_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret));
        }

    private:
        std::function<void(bcos::Error::Ptr _error)> m_callback;
    };

    std::vector<vector<tars::Char>> tarsConnectedNodes;
    for (auto const& it : _connectedNodes)
    {
        auto nodeID = it->data();
        tarsConnectedNodes.emplace_back(nodeID.begin(), nodeID.end());
    }
    m_proxy->async_notifyConnectedNodes(new Callback(_onRecvResponse), tarsConnectedNodes);
}
void bcostars::TxPoolServiceClient::notifyConsensusNodeList(
    bcos::consensus::ConsensusNodeList const& _consensusNodeList,
    std::function<void(bcos::Error::Ptr)> _onRecvResponse)
{
    class Callback : public bcostars::TxPoolServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr _error)> callback) : m_callback(callback) {}

        void callback_notifyConsensusNodeList(const bcostars::Error& ret) override
        {
            m_callback(toBcosError(ret));
        }

        void callback_notifyConsensusNodeList_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret));
        }

    private:
        std::function<void(bcos::Error::Ptr _error)> m_callback;
    };

    std::vector<bcostars::ConsensusNode> tarsConsensusNodeList;
    for (auto const& it : _consensusNodeList)
    {
        bcostars::ConsensusNode node;

        auto nodeID = it.nodeID->data();
        node.nodeID.assign(nodeID.begin(), nodeID.end());
        node.voteWeight = it.voteWeight;
        node.termWeight = it.termWeight;
        node.enableNumber = it.enableNumber;
        tarsConsensusNodeList.emplace_back(node);
    }

    m_proxy->async_notifyConsensusNodeList(new Callback(_onRecvResponse), tarsConsensusNodeList);
}
void bcostars::TxPoolServiceClient::notifyObserverNodeList(
    bcos::consensus::ConsensusNodeList const& _observerNodeList,
    std::function<void(bcos::Error::Ptr)> _onRecvResponse)
{
    class Callback : public bcostars::TxPoolServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr _error)> callback) : m_callback(callback) {}

        void callback_notifyObserverNodeList(const bcostars::Error& ret) override
        {
            m_callback(toBcosError(ret));
        }

        void callback_notifyObserverNodeList_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret));
        }

    private:
        std::function<void(bcos::Error::Ptr _error)> m_callback;
    };

    std::vector<bcostars::ConsensusNode> tarsConsensusNodeList;
    for (auto const& it : _observerNodeList)
    {
        bcostars::ConsensusNode node;
        auto nodeID = it.nodeID->data();
        node.nodeID.assign(nodeID.begin(), nodeID.end());
        node.voteWeight = it.voteWeight;
        node.termWeight = it.termWeight;
        node.enableNumber = it.enableNumber;
        tarsConsensusNodeList.emplace_back(node);
    }

    m_proxy->async_notifyObserverNodeList(new Callback(_onRecvResponse), tarsConsensusNodeList);
}
void bcostars::TxPoolServiceClient::asyncGetPendingTransactionSize(
    std::function<void(bcos::Error::Ptr, uint64_t)> _onGetTxsSize)
{
    class Callback : public TxPoolServicePrxCallback
    {
    public:
        explicit Callback(std::function<void(bcos::Error::Ptr, uint64_t)> _callback)
          : TxPoolServicePrxCallback(), m_callback(_callback)
        {}
        ~Callback() override {}

        void callback_asyncGetPendingTransactionSize(
            const bcostars::Error& ret, tars::Int64 _txsSize) override
        {
            m_callback(toBcosError(ret), _txsSize);
        }
        void callback_asyncGetPendingTransactionSize_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret), 0);
        }

    private:
        std::function<void(bcos::Error::Ptr, uint64_t)> m_callback;
    };
    m_proxy->async_asyncGetPendingTransactionSize(new Callback(_onGetTxsSize));
}
void bcostars::TxPoolServiceClient::asyncResetTxPool(std::function<void(bcos::Error::Ptr)> _onRecv)
{
    class Callback : public TxPoolServicePrxCallback
    {
    public:
        explicit Callback(std::function<void(bcos::Error::Ptr)> _callback)
          : TxPoolServicePrxCallback(), m_callback(_callback)
        {}
        ~Callback() override {}

        void callback_asyncResetTxPool(const bcostars::Error& ret) override
        {
            m_callback(toBcosError(ret));
        }
        void callback_asyncResetTxPool_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret));
        }

    private:
        std::function<void(bcos::Error::Ptr)> m_callback;
    };
    m_proxy->async_asyncResetTxPool(new Callback(_onRecv));
}
void bcostars::TxPoolServiceClient::start() {}
void bcostars::TxPoolServiceClient::stop() {}
