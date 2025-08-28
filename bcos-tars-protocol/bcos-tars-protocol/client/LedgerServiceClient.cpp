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

using namespace bcostars;

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
            const bcostars::Error& ret, const vector<tars::Char>& _blockHash) override
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
            const vector<bcostars::Transaction>& _txs,
            const map<std::string, vector<std::string>>& _merkleProofList) override
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
    std::vector<vector<tars::Char>> tarsTxsHashList;
    for (auto const& txHash : *_txHashList)
    {
        tarsTxsHashList.emplace_back(vector<tars::Char>(txHash.begin(), txHash.end()));
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
            const vector<std::string>& _proof) override
        {
            auto bcosReceipt = std::make_shared<bcostars::protocol::TransactionReceiptImpl>(
                [m_receipt = std::move(_receipt)]() mutable { return &m_receipt; });
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
        void callback_asyncGetNodeListByType(
            const bcostars::Error& ret, const vector<bcostars::ConsensusNode>& _nodeList) override
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
void bcostars::LedgerServiceClient::asyncPrewriteBlock(bcos::storage::StorageInterface::Ptr,
    bcos::protocol::ConstTransactionsPtr, bcos::protocol::Block::ConstPtr,
    std::function<void(std::string, bcos::Error::Ptr&&)>, bool,
    std::optional<bcos::ledger::Features>)
{
    BCOS_LOG(ERROR) << LOG_DESC("unimplemented method asyncPrewriteBlock");
}
void bcostars::LedgerServiceClient::asyncPreStoreBlockTxs(bcos::protocol::ConstTransactionsPtr,
    bcos::protocol::Block::ConstPtr, std::function<void(bcos::Error::UniquePtr&&)>)
{
    BCOS_LOG(ERROR) << LOG_DESC("unimplemented method asyncPreStoreBlockTxs");
}
bcos::Error::Ptr bcostars::LedgerServiceClient::storeTransactionsAndReceipts(
    bcos::protocol::ConstTransactionsPtr, bcos::protocol::Block::ConstPtr)
{
    BCOS_LOG(ERROR) << LOG_DESC("unimplemented method");
    return nullptr;
}
void bcostars::LedgerServiceClient::asyncGetCurrentStateByKey(std::string_view const& _key,
    std::function<void(bcos::Error::Ptr&&, std::optional<bcos::storage::Entry>&&)> _callback)

{
    BCOS_LOG(ERROR) << LOG_DESC("unimplemented method asyncGetCurrentStateByKey");
}
bcos::Error::Ptr bcostars::LedgerServiceClient::setCurrentStateByKey(
    std::string_view const& _key, bcos::storage::Entry entry)
{
    BCOS_LOG(ERROR) << LOG_DESC("unimplemented method setCurrentStateByKey");
    return nullptr;
}
void bcostars::LedgerServiceClient::asyncGetNonceList(bcos::protocol::BlockNumber, int64_t,
    std::function<void(bcos::Error::Ptr,
        std::shared_ptr<std::map<bcos::protocol::BlockNumber, bcos::protocol::NonceListPtr>>)>)

{
    BCOS_LOG(ERROR) << LOG_DESC("unimplement method asyncGetNonceList");
}
void bcostars::LedgerServiceClient::removeExpiredNonce(
    bcos::protocol::BlockNumber blockNumber, bool sync)
{
    BCOS_LOG(ERROR) << LOG_DESC("unimplement method asyncGetNonceList");
}
