#pragma once

#include <boost/throw_exception.hpp>
#include <variant>
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"

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
#include <memory>

namespace bcostars
{
class TxPoolServiceClient : public bcos::txpool::TxPoolInterface
{
public:
    TxPoolServiceClient(bcostars::TxPoolServicePrx _proxy,
        bcos::crypto::CryptoSuite::Ptr _cryptoSuite,
        bcos::protocol::BlockFactory::Ptr _blockFactory)
      : m_proxy(_proxy), m_cryptoSuite(_cryptoSuite), m_blockFactory(_blockFactory)
    {}
    ~TxPoolServiceClient() override {}

    bcos::task::Task<bcos::protocol::TransactionSubmitResult::Ptr> submitTransaction(
        bcos::protocol::Transaction::Ptr transaction) override
    {
        struct TarsCallback : public bcostars::TxPoolServicePrxCallback
        {
            void callback_submit(const bcostars::Error& ret,
                const bcostars::TransactionSubmitResult& result) override
            {
                auto bcosResult = std::make_shared<bcostars::protocol::TransactionSubmitResultImpl>(
                    std::move(m_cryptoSuite),
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

            CO_STD::coroutine_handle<> m_handle;
            std::variant<std::monostate, bcos::Error::Ptr,
                bcostars::protocol::TransactionSubmitResultImpl::Ptr>
                m_submitResult;
            bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
        };

        struct Awaitable
        {
            constexpr bool await_ready() { return false; }
            void await_suspend(CO_STD::coroutine_handle<> handle)
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
        };

        auto tarsCallback = std::make_unique<TarsCallback>();
        tarsCallback->m_cryptoSuite = m_cryptoSuite;

        auto awaitable = Awaitable{.m_callback = tarsCallback.release(),
            .m_transaction = std::move(transaction),
            .m_proxy = m_proxy};

        co_return co_await awaitable;
    }

    void asyncSealTxs(uint64_t _txsLimit, bcos::txpool::TxsHashSetPtr _avoidTxs,
        std::function<void(
            bcos::Error::Ptr, bcos::protocol::Block::Ptr, bcos::protocol::Block::Ptr)>
            _sealCallback) override
    {
        class Callback : public bcostars::TxPoolServicePrxCallback
        {
        public:
            Callback(bcos::protocol::BlockFactory::Ptr _blockFactory,
                std::function<void(
                    bcos::Error::Ptr, bcos::protocol::Block::Ptr, bcos::protocol::Block::Ptr)>
                    callback)
              : m_blockFactory(_blockFactory), m_callback(callback)
            {}

            void callback_asyncSealTxs(const bcostars::Error& ret, const bcostars::Block& _txsList,
                const bcostars::Block& _sysTxList) override
            {
                auto txsList = m_blockFactory->createBlock();
                std::dynamic_pointer_cast<bcostars::protocol::BlockImpl>(txsList)->setInner(
                    std::move(*const_cast<bcostars::Block*>(&_txsList)));

                auto sysTxList = m_blockFactory->createBlock();
                std::dynamic_pointer_cast<bcostars::protocol::BlockImpl>(sysTxList)->setInner(
                    std::move(*const_cast<bcostars::Block*>(&_sysTxList)));

                m_callback(toBcosError(ret), txsList, sysTxList);
            }

            void callback_asyncSealTxs_exception(tars::Int32 ret) override
            {
                m_callback(toBcosError(ret), nullptr, nullptr);
            }

        private:
            bcos::protocol::BlockFactory::Ptr m_blockFactory;
            std::function<void(
                bcos::Error::Ptr, bcos::protocol::Block::Ptr, bcos::protocol::Block::Ptr)>
                m_callback;
        };

        vector<vector<tars::Char>> tarsAvoidTxs;
        if (_avoidTxs && _avoidTxs->size() > 0)
        {
            for (auto& it : *_avoidTxs)
            {
                tarsAvoidTxs.push_back(std::vector<char>(it.begin(), it.end()));
            }
        }

        m_proxy->async_asyncSealTxs(
            new Callback(m_blockFactory, _sealCallback), _txsLimit, tarsAvoidTxs);
    }

    void asyncMarkTxs(bcos::crypto::HashListPtr _txsHash, bool _sealedFlag,
        bcos::protocol::BlockNumber _batchId, bcos::crypto::HashType const& _batchHash,
        std::function<void(bcos::Error::Ptr)> _onRecvResponse) override
    {
        class Callback : public bcostars::TxPoolServicePrxCallback
        {
        public:
            Callback(std::function<void(bcos::Error::Ptr)> callback) : m_callback(callback) {}

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
        for (auto& it : *_txsHash)
        {
            txHashList.push_back(std::vector<char>(it.begin(), it.end()));
        }

        m_proxy->async_asyncMarkTxs(new Callback(_onRecvResponse), txHashList, _sealedFlag,
            _batchId, std::vector<char>(_batchHash.begin(), _batchHash.end()));
    }

    void asyncVerifyBlock(bcos::crypto::PublicPtr _generatedNodeID,
        bcos::bytesConstRef const& _block,
        std::function<void(bcos::Error::Ptr, bool)> _onVerifyFinished) override
    {
        class Callback : public bcostars::TxPoolServicePrxCallback
        {
        public:
            Callback(std::function<void(bcos::Error::Ptr, bool)> callback) : m_callback(callback) {}

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

        auto nodeID = _generatedNodeID->data();
        m_proxy->async_asyncVerifyBlock(new Callback(_onVerifyFinished),
            std::vector<char>(nodeID.begin(), nodeID.end()),
            std::vector<char>(_block.begin(), _block.end()));
    }

    void asyncFillBlock(bcos::crypto::HashListPtr _txsHash,
        std::function<void(bcos::Error::Ptr, bcos::protocol::TransactionsPtr)> _onBlockFilled)
        override
    {
        class Callback : public bcostars::TxPoolServicePrxCallback
        {
        public:
            Callback(
                std::function<void(bcos::Error::Ptr, bcos::protocol::TransactionsPtr)> callback,
                bcos::crypto::CryptoSuite::Ptr cryptoSuite)
              : m_callback(callback), m_cryptoSuite(cryptoSuite)
            {}

            void callback_asyncFillBlock(
                const bcostars::Error& ret, const vector<bcostars::Transaction>& filled) override
            {
                auto mutableFilled = const_cast<vector<bcostars::Transaction>*>(&filled);
                auto txs = std::make_shared<bcos::protocol::Transactions>();
                for (auto&& it : *mutableFilled)
                {
                    auto tx = std::make_shared<bcostars::protocol::TransactionImpl>(
                        m_cryptoSuite, [m_tx = std::move(it)]() mutable { return &m_tx; });
                    txs->push_back(tx);
                }
                m_callback(toBcosError(ret), txs);
            }

            void callback_asyncFillBlock_exception(tars::Int32 ret) override
            {
                m_callback(toBcosError(ret), nullptr);
            }

        private:
            std::function<void(bcos::Error::Ptr, bcos::protocol::TransactionsPtr)> m_callback;
            bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
        };

        vector<vector<tars::Char>> hashList;
        for (auto hashData : *_txsHash)
        {
            hashList.emplace_back(hashData.begin(), hashData.end());
        }

        m_proxy->async_asyncFillBlock(new Callback(_onBlockFilled, m_cryptoSuite), hashList);
    }

    void asyncNotifyBlockResult(bcos::protocol::BlockNumber _blockNumber,
        bcos::protocol::TransactionSubmitResultsPtr _txsResult,
        std::function<void(bcos::Error::Ptr)> _onNotifyFinished) override
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

    void asyncNotifyTxsSyncMessage(bcos::Error::Ptr _error, std::string const& _id,
        bcos::crypto::NodeIDPtr _nodeID, bcos::bytesConstRef _data,
        std::function<void(bcos::Error::Ptr _error)> _onRecv) override
    {
        class Callback : public bcostars::TxPoolServicePrxCallback
        {
        public:
            Callback(std::function<void(bcos::Error::Ptr _error)> callback) : m_callback(callback)
            {}

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

    void notifyConnectedNodes(bcos::crypto::NodeIDSet const& _connectedNodes,
        std::function<void(bcos::Error::Ptr)> _onRecvResponse) override
    {
        class Callback : public bcostars::TxPoolServicePrxCallback
        {
        public:
            Callback(std::function<void(bcos::Error::Ptr _error)> callback) : m_callback(callback)
            {}

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

    void notifyConsensusNodeList(bcos::consensus::ConsensusNodeList const& _consensusNodeList,
        std::function<void(bcos::Error::Ptr)> _onRecvResponse) override
    {
        class Callback : public bcostars::TxPoolServicePrxCallback
        {
        public:
            Callback(std::function<void(bcos::Error::Ptr _error)> callback) : m_callback(callback)
            {}

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

            auto nodeID = it->nodeID()->data();
            node.nodeID.assign(nodeID.begin(), nodeID.end());
            node.weight = it->weight();
            tarsConsensusNodeList.emplace_back(node);
        }

        m_proxy->async_notifyConsensusNodeList(
            new Callback(_onRecvResponse), tarsConsensusNodeList);
    }

    void notifyObserverNodeList(bcos::consensus::ConsensusNodeList const& _observerNodeList,
        std::function<void(bcos::Error::Ptr)> _onRecvResponse) override
    {
        class Callback : public bcostars::TxPoolServicePrxCallback
        {
        public:
            Callback(std::function<void(bcos::Error::Ptr _error)> callback) : m_callback(callback)
            {}

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
            auto nodeID = it->nodeID()->data();
            node.nodeID.assign(nodeID.begin(), nodeID.end());
            node.weight = it->weight();
            tarsConsensusNodeList.emplace_back(node);
        }

        m_proxy->async_notifyObserverNodeList(new Callback(_onRecvResponse), tarsConsensusNodeList);
    }

    // for RPC to get pending transactions
    void asyncGetPendingTransactionSize(
        std::function<void(bcos::Error::Ptr, uint64_t)> _onGetTxsSize) override
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

    void asyncResetTxPool(std::function<void(bcos::Error::Ptr)> _onRecv) override
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

protected:
    void start() override {}
    void stop() override {}

private:
    bcostars::TxPoolServicePrx m_proxy;
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
};

}  // namespace bcostars