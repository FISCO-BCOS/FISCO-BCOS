/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file LedgerServiceClient.cpp
 * @author: yujiechen
 * @date 2021-10-17
 */
#include "LedgerServiceClient.h"
#include "../Common.h"
#include "../ErrorConverter.h"
#include "../protocol/BlockImpl.h"
#include "../protocol/TransactionImpl.h"
#include "../protocol/TransactionReceiptImpl.h"
#include <bcos-task/OneShot.h>
#include <bcos-utilities/LogStream.h>
#include <boost/throw_exception.hpp>

using namespace bcostars;

namespace
{
template <typename Result, typename Initiate>
bcos::task::Task<std::pair<bcos::Error::Ptr, Result>> awaitCallback(Initiate initiate)
{
    auto done = std::make_shared<bcos::task::OneShot<bcos::Error::Ptr, Result>>();
    initiate([done](bcos::Error::Ptr error, Result result) mutable {
        done->complete(std::move(error), std::move(result));
    });
    auto [error, result] = co_await *done;
    co_return {std::move(error), std::move(result)};
}

template <typename Result, typename Initiate>
bcos::task::Task<Result> awaitResult(Initiate initiate)
{
    auto [error, result] = co_await awaitCallback<Result>(std::move(initiate));
    if (error)
    {
        BOOST_THROW_EXCEPTION(*error);
    }
    co_return std::move(result);
}
}  // namespace

void LedgerServiceClient::asyncGetBlockDataByNumber(bcos::protocol::BlockNumber _blockNumber,
    int32_t _blockFlag,
    std::function<void(bcos::Error::Ptr, bcos::protocol::Block::Ptr)> _onGetBlock)
{
    class Callback : public LedgerServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr, bcos::protocol::Block::Ptr)> _callback,
            bcos::protocol::BlockFactory::Ptr _blockFactory)
          : m_callback(_callback), m_blockFactory(_blockFactory)
        {}
        void callback_asyncGetBlockDataByNumber(
            const bcostars::Error& ret, const bcostars::Block& _block) override
        {
            auto bcosBlock = m_blockFactory->createBlock();
            auto tarsBlock = std::dynamic_pointer_cast<bcostars::protocol::BlockImpl>(bcosBlock);
            tarsBlock->setInner(std::move(*const_cast<bcostars::Block*>(&_block)));
            m_callback(toBcosError(ret), tarsBlock);
        }
        void callback_asyncGetBlockDataByNumber_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret), nullptr);
        }

    private:
        std::function<void(bcos::Error::Ptr, bcos::protocol::Block::Ptr)> m_callback;
        bcos::protocol::BlockFactory::Ptr m_blockFactory;
    };
    m_prx->async_asyncGetBlockDataByNumber(
        new Callback(_onGetBlock, m_blockFactory), _blockNumber, _blockFlag);
}

void LedgerServiceClient::asyncGetBlockNumber(
    std::function<void(bcos::Error::Ptr, bcos::protocol::BlockNumber)> _onGetBlock)
{
    class Callback : public LedgerServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr, bcos::protocol::BlockNumber)> _callback)
          : m_callback(_callback)
        {}
        void callback_asyncGetBlockNumber(
            const bcostars::Error& ret, tars::Int64 _blockNumber) override
        {
            m_callback(toBcosError(ret), _blockNumber);
        }
        void callback_asyncGetBlockNumber_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret), -1);
        }

    private:
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockNumber)> m_callback;
    };
    m_prx->async_asyncGetBlockNumber(new Callback(_onGetBlock));
}

void LedgerServiceClient::asyncGetBlockHashByNumber(bcos::protocol::BlockNumber _blockNumber,
    std::function<void(bcos::Error::Ptr, bcos::crypto::HashType)> _onGetBlock)
{
    class Callback : public LedgerServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr, bcos::crypto::HashType)> _callback)
          : m_callback(_callback)
        {}
        void callback_asyncGetBlockHashByNumber(
            const bcostars::Error& ret, const std::vector<tars::Char>& _blockHash) override
        {
            if (_blockHash.size() >= bcos::crypto::HashType::SIZE)
            {
                auto hashData =
                    bcos::crypto::HashType(reinterpret_cast<const bcos::byte*>(_blockHash.data()),
                        bcos::crypto::HashType::SIZE);
                m_callback(toBcosError(ret), hashData);
                return;
            }
            m_callback(toBcosError(ret), bcos::crypto::HashType());
        }
        void callback_asyncGetBlockHashByNumber_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret), bcos::crypto::HashType());
        }

    private:
        std::function<void(bcos::Error::Ptr, bcos::crypto::HashType const&)> m_callback;
    };
    m_prx->async_asyncGetBlockHashByNumber(new Callback(_onGetBlock), _blockNumber);
}

void LedgerServiceClient::asyncGetBlockNumberByHash(bcos::crypto::HashType const& _blockHash,
    std::function<void(bcos::Error::Ptr, bcos::protocol::BlockNumber)> _onGetBlock)
{
    class Callback : public LedgerServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr, bcos::protocol::BlockNumber)> _callback)
          : m_callback(_callback)
        {}
        void callback_asyncGetBlockNumberByHash(
            const bcostars::Error& ret, tars::Int64 _blockNumber) override
        {
            m_callback(toBcosError(ret), _blockNumber);
        }
        void callback_asyncGetBlockNumberByHash_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret), -1);
        }

    private:
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockNumber)> m_callback;
    };
    std::vector<tars::Char> blockHash(_blockHash.begin(), _blockHash.end());
    m_prx->async_asyncGetBlockNumberByHash(new Callback(_onGetBlock), blockHash);
}

void LedgerServiceClient::asyncGetBatchTxsByHashList(bcos::crypto::HashListPtr _txHashList,
    bool _withProof,
    std::function<void(bcos::Error::Ptr, bcos::protocol::TransactionsPtr,
        std::shared_ptr<std::map<std::string, bcos::ledger::MerkleProofPtr>>)>
        _onGetTx)
{
    class Callback : public LedgerServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr, bcos::protocol::TransactionsPtr,
                     std::shared_ptr<std::map<std::string, bcos::ledger::MerkleProofPtr>>)>
                     _callback,
            bcos::crypto::CryptoSuite::Ptr _cryptoSuite)
          : m_callback(_callback), m_cryptoSuite(_cryptoSuite)
        {}

        void callback_asyncGetBatchTxsByHashList(const bcostars::Error& ret,
            const std::vector<bcostars::Transaction>& _txs,
            const std::map<std::string, std::vector<std::string>>& _merkleProofList) override
        {
            // decode the txsList
            auto bcosTxsList = std::make_shared<bcos::protocol::Transactions>();
            for (auto const& tx : _txs)
            {
                auto bcosTx = std::make_shared<bcostars::protocol::TransactionImpl>(
                    [m_tx = tx]() mutable { return &m_tx; });
                bcosTxsList->emplace_back(bcosTx);
            }
            // decode the proof list
            auto proofList =
                std::make_shared<std::map<std::string, bcos::ledger::MerkleProofPtr>>();
            for (auto const& it : _merkleProofList)
            {
                auto const& proof = it.second;
                auto bcosProof = std::make_shared<bcos::ledger::MerkleProof>();
                for (auto const& item : proof)
                {
                    bcosProof->emplace_back(bcos::fromHex(item));
                }
                (*proofList)[it.first] = bcosProof;
            }
            m_callback(toBcosError(ret), bcosTxsList, proofList);
        }
        void callback_asyncGetBatchTxsByHashList_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret), nullptr, nullptr);
        }

    private:
        std::function<void(bcos::Error::Ptr, bcos::protocol::TransactionsPtr,
            std::shared_ptr<std::map<std::string, bcos::ledger::MerkleProofPtr>>)>
            m_callback;
        bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    };
    std::vector<std::vector<tars::Char>> tarsTxsHashList;
    for (auto const& txHash : *_txHashList)
    {
        tarsTxsHashList.emplace_back(txHash.begin(), txHash.end());
    }
    m_prx->async_asyncGetBatchTxsByHashList(
        new Callback(_onGetTx, m_cryptoSuite), tarsTxsHashList, _withProof);
}

void LedgerServiceClient::asyncGetTransactionReceiptByHash(bcos::crypto::HashType const& _txHash,
    bool _withProof,
    std::function<void(
        bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr, bcos::ledger::MerkleProofPtr)>
        _onGetTx)
{
    class Callback : public LedgerServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr,
                     bcos::ledger::MerkleProofPtr)>
                     _callback,
            bcos::crypto::CryptoSuite::Ptr _cryptoSuite)
          : m_callback(_callback), m_cryptoSuite(_cryptoSuite)
        {}
        void callback_asyncGetTransactionReceiptByHash(const bcostars::Error& ret,
            const bcostars::TransactionReceipt& _receipt,
            const std::vector<std::string>& _proof) override
        {
            auto bcosReceipt = protocol::makeTransactionReceiptFromTars(_receipt);
            auto bcosProof = std::make_shared<bcos::ledger::MerkleProof>();
            for (auto const& item : _proof)
            {
                bcosProof->emplace_back(bcos::fromHex(item));
            }
            m_callback(toBcosError(ret), bcosReceipt, bcosProof);
        }
        void callback_asyncGetTransactionReceiptByHash_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret), nullptr, nullptr);
        }

    private:
        std::function<void(bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr,
            bcos::ledger::MerkleProofPtr)>
            m_callback;
        bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    };
    std::vector<tars::Char> txHash(_txHash.begin(), _txHash.end());
    m_prx->async_asyncGetTransactionReceiptByHash(
        new Callback(_onGetTx, m_cryptoSuite), txHash, _withProof);
}

void LedgerServiceClient::asyncGetTotalTransactionCount(
    std::function<void(bcos::Error::Ptr, int64_t _totalTxCount, int64_t _failedTxCount,
        bcos::protocol::BlockNumber _latestBlockNumber)>
        _callback)
{
    class Callback : public LedgerServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr, int64_t _totalTxCount, int64_t _failedTxCount,
                bcos::protocol::BlockNumber _latestBlockNumber)>
                _callback)
          : m_callback(_callback)
        {}
        void callback_asyncGetTotalTransactionCount(const bcostars::Error& ret,
            tars::Int64 _totalTxCount, tars::Int64 _failedTxCount,
            tars::Int64 _latestBlockNumber) override
        {
            m_callback(toBcosError(ret), _totalTxCount, _failedTxCount, _latestBlockNumber);
        }
        void callback_asyncGetTotalTransactionCount_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret), -1, -1, -1);
        }

    private:
        std::function<void(bcos::Error::Ptr, int64_t _totalTxCount, int64_t _failedTxCount,
            bcos::protocol::BlockNumber _latestBlockNumber)>
            m_callback;
    };
    m_prx->async_asyncGetTotalTransactionCount(new Callback(_callback));
}

void LedgerServiceClient::asyncGetSystemConfigByKey(std::string_view const& _key,
    std::function<void(bcos::Error::Ptr, std::string, bcos::protocol::BlockNumber)> _onGetConfig)
{
    class Callback : public LedgerServicePrxCallback
    {
    public:
        Callback(std::function<void(bcos::Error::Ptr, std::string, bcos::protocol::BlockNumber)>
                _callback)
          : m_callback(_callback)
        {}
        void callback_asyncGetSystemConfigByKey(const bcostars::Error& ret,
            const std::string& _value, tars::Int64 _blockNumber) override
        {
            m_callback(toBcosError(ret), _value, _blockNumber);
        }
        void callback_asyncGetSystemConfigByKey_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret), "", -1);
        }

    private:
        std::function<void(bcos::Error::Ptr, std::string, bcos::protocol::BlockNumber)> m_callback;
    };
    m_prx->async_asyncGetSystemConfigByKey(new Callback(_onGetConfig), std::string{_key});
}

void LedgerServiceClient::asyncGetNodeListByType(std::string_view const& _type,
    std::function<void(bcos::Error::Ptr, bcos::consensus::ConsensusNodeList)> _onGetConfig)
{
    auto type = magic_enum::enum_cast<bcos::consensus::Type>(_type);
    class Callback : public LedgerServicePrxCallback
    {
    public:
        Callback(
            std::function<void(bcos::Error::Ptr, bcos::consensus::ConsensusNodeList)> m_callback,
            bcos::crypto::KeyFactory::Ptr m_keyFactory, bcos::consensus::Type m_type)
          : m_callback(std::move(m_callback)), m_keyFactory(std::move(m_keyFactory)), m_type(m_type)
        {}
        void callback_asyncGetNodeListByType(const bcostars::Error& ret,
            const std::vector<bcostars::ConsensusNode>& _nodeList) override
        {
            m_callback(toBcosError(ret), toConsensusNodeList(m_keyFactory, m_type, _nodeList));
        }
        void callback_asyncGetNodeListByType_exception(tars::Int32 ret) override
        {
            m_callback(toBcosError(ret), {});
        }

    private:
        std::function<void(bcos::Error::Ptr, bcos::consensus::ConsensusNodeList)> m_callback;
        bcos::crypto::KeyFactory::Ptr m_keyFactory;
        bcos::consensus::Type m_type;
    };
    m_prx->async_asyncGetNodeListByType(
        new Callback{std::move(_onGetConfig), m_keyFactory, *type}, std::string{_type});
}
bcostars::LedgerServiceClient::LedgerServiceClient(
    bcostars::LedgerServicePrx _prx, bcos::protocol::BlockFactory::Ptr _blockFactory)
  : m_prx(_prx), m_blockFactory(_blockFactory)
{
    if (m_blockFactory)
    {
        m_cryptoSuite = m_blockFactory->cryptoSuite();
        m_keyFactory = m_cryptoSuite->keyFactory();
    }
}
bcostars::LedgerServiceClient::~LedgerServiceClient() {}
bcos::task::Task<std::string> bcostars::LedgerServiceClient::prewriteBlock(
    bcos::storage::StorageInterface::Ptr, bcos::protocol::ConstTransactionsPtr,
    bcos::protocol::Block::ConstPtr, bool,
    std::optional<bcos::crypto::HashType> /*blockHashOverride*/, bool /*writeNonces*/)
{
    BCOS_LOG(ERROR) << LOG_DESC("unimplemented method prewriteBlock");
    BOOST_THROW_EXCEPTION(BCOS_ERROR(bcos::ledger::LedgerError::CallbackError,
        "prewriteBlock is not implemented by LedgerServiceClient"));
    co_return std::string{};
}
bcos::task::Task<void> bcostars::LedgerServiceClient::preStoreBlockTxs(
    bcos::protocol::ConstTransactionsPtr, bcos::protocol::Block::ConstPtr)
{
    BCOS_LOG(ERROR) << LOG_DESC("unimplemented method preStoreBlockTxs");
    BOOST_THROW_EXCEPTION(BCOS_ERROR(bcos::ledger::LedgerError::CallbackError,
        "preStoreBlockTxs is not implemented by LedgerServiceClient"));
    co_return;
}

bcos::Error::Ptr bcostars::LedgerServiceClient::storeTransactionsAndReceipts(
    bcos::protocol::ConstTransactionsPtr, bcos::protocol::Block::ConstPtr)
{
    BCOS_LOG(ERROR) << LOG_DESC("unimplemented method");
    return nullptr;
}

bcos::task::Task<bcos::protocol::Block::Ptr> bcostars::LedgerServiceClient::getBlockDataByNumber(
    bcos::protocol::BlockNumber blockNumber, int32_t blockFlag)
{
    return awaitResult<bcos::protocol::Block::Ptr>([this, blockNumber, blockFlag](auto callback) {
        asyncGetBlockDataByNumber(blockNumber, blockFlag, std::move(callback));
    });
}

bcos::task::Task<bcos::protocol::BlockNumber> bcostars::LedgerServiceClient::getBlockNumber()
{
    return awaitResult<bcos::protocol::BlockNumber>(
        [this](auto callback) { asyncGetBlockNumber(std::move(callback)); });
}

bcos::task::Task<bcos::crypto::HashType> bcostars::LedgerServiceClient::getBlockHashByNumber(
    bcos::protocol::BlockNumber blockNumber)
{
    return awaitResult<bcos::crypto::HashType>([this, blockNumber](auto callback) {
        asyncGetBlockHashByNumber(blockNumber, std::move(callback));
    });
}

bcos::task::Task<bcos::protocol::BlockNumber> bcostars::LedgerServiceClient::getBlockNumberByHash(
    bcos::crypto::HashType blockHash)
{
    return awaitResult<bcos::protocol::BlockNumber>(
        [this, blockHash = std::move(blockHash)](
            auto callback) { asyncGetBlockNumberByHash(blockHash, std::move(callback)); });
}

bcos::task::Task<bcos::ledger::BatchTransactionsResult>
bcostars::LedgerServiceClient::getBatchTxsByHashList(
    bcos::crypto::HashListPtr txHashList, bool withProof)
{
    return awaitResult<bcos::ledger::BatchTransactionsResult>(
        [this, txHashList = std::move(txHashList), withProof](auto callback) mutable {
            asyncGetBatchTxsByHashList(txHashList, withProof,
                [callback = std::move(callback)](bcos::Error::Ptr error,
                    bcos::protocol::TransactionsPtr transactions,
                    bcos::ledger::MerkleProofMapPtr proofs) mutable {
                    callback(std::move(error), bcos::ledger::BatchTransactionsResult{
                                                   std::move(transactions), std::move(proofs)});
                });
        });
}

bcos::task::Task<bcos::ledger::TransactionReceiptResult>
bcostars::LedgerServiceClient::getTransactionReceiptByHash(
    bcos::crypto::HashType txHash, bool withProof)
{
    return [](LedgerServiceClient& self, bcos::crypto::HashType hash,
               bool requestProof) -> bcos::task::Task<bcos::ledger::TransactionReceiptResult> {
        auto [error, result] = co_await awaitCallback<bcos::ledger::TransactionReceiptResult>(
            [&self, hash = std::move(hash), requestProof](auto callback) mutable {
                self.asyncGetTransactionReceiptByHash(hash, requestProof,
                    [callback = std::move(callback)](bcos::Error::Ptr callbackError,
                        bcos::protocol::TransactionReceipt::Ptr receipt,
                        bcos::ledger::MerkleProofPtr proof) mutable {
                        callback(std::move(callbackError),
                            bcos::ledger::TransactionReceiptResult{
                                std::move(receipt), std::move(proof), nullptr});
                    });
            });
        if (error && (!requestProof || !result.receipt))
        {
            BOOST_THROW_EXCEPTION(*error);
        }
        result.error = std::move(error);
        co_return result;
    }(*this, std::move(txHash), withProof);
}

bcos::task::Task<bcos::ledger::TransactionCountResult>
bcostars::LedgerServiceClient::getTotalTransactionCount()
{
    return awaitResult<bcos::ledger::TransactionCountResult>([this](auto callback) {
        asyncGetTotalTransactionCount(
            [callback = std::move(callback)](bcos::Error::Ptr error, int64_t total, int64_t failed,
                bcos::protocol::BlockNumber blockNumber) mutable {
                callback(std::move(error),
                    bcos::ledger::TransactionCountResult{total, failed, blockNumber});
            });
    });
}

bcos::task::Task<std::optional<bcos::storage::Entry>>
bcostars::LedgerServiceClient::getCurrentStateByKey(std::string_view)
{
    BCOS_LOG(ERROR) << LOG_DESC("unimplemented method getCurrentStateByKey");
    BOOST_THROW_EXCEPTION(BCOS_ERROR(bcos::ledger::LedgerError::CallbackError,
        "getCurrentStateByKey is not implemented by LedgerServiceClient"));
    co_return std::nullopt;
}

bcos::Error::Ptr bcostars::LedgerServiceClient::setCurrentStateByKey(
    std::string_view const& _key, bcos::storage::Entry entry)
{
    BCOS_LOG(ERROR) << LOG_DESC("unimplemented method setCurrentStateByKey");
    return nullptr;
}

bcos::task::Task<bcos::ledger::SystemConfigEntry>
bcostars::LedgerServiceClient::getSystemConfigByKey(std::string_view key)
{
    return awaitResult<bcos::ledger::SystemConfigEntry>(
        [this, key = std::string(key)](auto callback) {
            asyncGetSystemConfigByKey(
                key, [callback = std::move(callback)](bcos::Error::Ptr error, std::string value,
                         bcos::protocol::BlockNumber blockNumber) mutable {
                    callback(std::move(error),
                        bcos::ledger::SystemConfigEntry{std::move(value), blockNumber});
                });
        });
}

bcos::task::Task<bcos::consensus::ConsensusNodeList>
bcostars::LedgerServiceClient::getNodeListByType(std::string_view type)
{
    return awaitResult<bcos::consensus::ConsensusNodeList>(
        [this, type = std::string(type)](
            auto callback) { asyncGetNodeListByType(type, std::move(callback)); });
}

bcos::task::Task<bcos::ledger::NonceListResult> bcostars::LedgerServiceClient::getNonceList(
    bcos::protocol::BlockNumber, int64_t)
{
    BCOS_LOG(ERROR) << LOG_DESC("unimplement method getNonceList");
    BOOST_THROW_EXCEPTION(BCOS_ERROR(bcos::ledger::LedgerError::CallbackError,
        "getNonceList is not implemented by LedgerServiceClient"));
    co_return nullptr;
}

void bcostars::LedgerServiceClient::removeExpiredNonce(
    bcos::protocol::BlockNumber blockNumber, bool sync)
{
    BCOS_LOG(ERROR) << LOG_DESC("unimplement method asyncGetNonceList");
}
