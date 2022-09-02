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
 * @brief server for Ledger
 * @file LedgerServiceServer.cpp
 * @author: yujiechen
 * @date 2021-10-18
 */

#include "LedgerServiceServer.h"
#include <bcos-tars-protocol/Common.h>
#include <bcos-tars-protocol/ErrorConverter.h>
#include <bcos-tars-protocol/protocol/BlockImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptImpl.h>

using namespace bcostars;

bcostars::Error LedgerServiceServer::asyncGetBatchTxsByHashList(
    const vector<vector<tars::Char>>& _txsHashList, tars::Bool _withProof,
    vector<bcostars::Transaction>&, map<std::string, vector<bcostars::MerkleProofItem>>&,
    tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    auto hashList = std::make_shared<bcos::crypto::HashList>();
    for (auto const& hash : _txsHashList)
    {
        if (hash.size() < bcos::crypto::HashType::SIZE)
        {
            continue;
        }
        hashList->emplace_back(bcos::crypto::HashType(
            reinterpret_cast<const bcos::byte*>(hash.data()), bcos::crypto::HashType::SIZE));
    }
    m_ledger->asyncGetBatchTxsByHashList(hashList, _withProof,
        [current](bcos::Error::Ptr _error, bcos::protocol::TransactionsPtr _txsList,
            std::shared_ptr<std::map<std::string, bcos::ledger::MerkleProofPtr>> _proofList) {
            // to tars transaction
            std::vector<bcostars::Transaction> tarsTxs;
            if (_txsList)
            {
                for (auto const& tx : *_txsList)
                {
                    tarsTxs.emplace_back(
                        std::dynamic_pointer_cast<bcostars::protocol::TransactionImpl>(tx)
                            ->inner());
                }
            }
            // to tars proof
            std::map<std::string, std::vector<bcostars::MerkleProofItem>> tarsMerkleProofs;
            if (_proofList)
            {
                for (auto const& it : *_proofList)
                {
                    std::vector<bcostars::MerkleProofItem> tarsMerkleItem;
                    auto const& proofs = it.second;
                    for (auto const& proof : *proofs)
                    {
                        MerkleProofItem tarsProof;
                        tarsProof.left = proof.first;
                        tarsProof.right = proof.second;
                        tarsMerkleItem.emplace_back(tarsProof);
                    }
                    tarsMerkleProofs[it.first] = tarsMerkleItem;
                }
            }
            async_response_asyncGetBatchTxsByHashList(
                current, toTarsError(_error), tarsTxs, tarsMerkleProofs);
        });
    return bcostars::Error();
}

bcostars::Error LedgerServiceServer::asyncGetBlockDataByNumber(tars::Int64 _blockNumber,
    tars::Int64 _blockFlag, bcostars::Block&, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    m_ledger->asyncGetBlockDataByNumber(_blockNumber, _blockFlag,
        [current](bcos::Error::Ptr _error, bcos::protocol::Block::Ptr _block) {
            bcostars::Block tarsBlock;
            if (_block)
            {
                tarsBlock =
                    std::dynamic_pointer_cast<bcostars::protocol::BlockImpl>(_block)->inner();
            }
            async_response_asyncGetBlockDataByNumber(current, toTarsError(_error), tarsBlock);
        });
    return bcostars::Error();
}

bcostars::Error LedgerServiceServer::asyncGetBlockHashByNumber(
    tars::Int64 _blockNumber, vector<tars::Char>&, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    m_ledger->asyncGetBlockHashByNumber(
        _blockNumber, [current](bcos::Error::Ptr _error, bcos::crypto::HashType const& _blockHash) {
            if (_error)
            {
                async_response_asyncGetBlockHashByNumber(
                    current, toTarsError(_error), vector<tars::Char>());
                return;
            }
            vector<tars::Char> blockHash(_blockHash.begin(), _blockHash.end());
            async_response_asyncGetBlockHashByNumber(current, toTarsError(_error), blockHash);
        });
    return bcostars::Error();
}

bcostars::Error LedgerServiceServer::asyncGetBlockNumber(tars::Int64&, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    m_ledger->asyncGetBlockNumber(
        [current](bcos::Error::Ptr _error, bcos::protocol::BlockNumber _blockNumber) {
            async_response_asyncGetBlockNumber(current, toTarsError(_error), _blockNumber);
        });
    return bcostars::Error();
}
bcostars::Error LedgerServiceServer::asyncGetBlockNumberByHash(
    const vector<tars::Char>& _blockHash, tars::Int64&, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    bcos::crypto::HashType blockHash;
    if (_blockHash.size() >= bcos::crypto::HashType::SIZE)
    {
        blockHash = bcos::crypto::HashType(
            reinterpret_cast<const bcos::byte*>(_blockHash.data()), bcos::crypto::HashType::SIZE);
    }
    // _blockHash
    m_ledger->asyncGetBlockNumberByHash(
        blockHash, [current](bcos::Error::Ptr _error, bcos::protocol::BlockNumber _blockNumber) {
            async_response_asyncGetBlockNumberByHash(current, toTarsError(_error), _blockNumber);
        });
    return bcostars::Error();
}

bcostars::Error LedgerServiceServer::asyncGetNodeListByType(
    const std::string& _type, vector<bcostars::ConsensusNode>&, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    m_ledger->asyncGetNodeListByType(
        _type, [current](bcos::Error::Ptr _error, bcos::consensus::ConsensusNodeListPtr _nodeList) {
            async_response_asyncGetNodeListByType(
                current, toTarsError(_error), toTarsConsensusNodeList(*_nodeList));
        });
    return bcostars::Error();
}

bcostars::Error LedgerServiceServer::asyncGetSystemConfigByKey(
    const std::string& _key, std::string&, tars::Int64&, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    m_ledger->asyncGetSystemConfigByKey(_key, [current](bcos::Error::Ptr _error, std::string _value,
                                                  bcos::protocol::BlockNumber _blockNumber) {
        async_response_asyncGetSystemConfigByKey(
            current, toTarsError(_error), _value, _blockNumber);
    });
    return bcostars::Error();
}
bcostars::Error LedgerServiceServer::asyncGetTotalTransactionCount(
    tars::Int64&, tars::Int64&, tars::Int64&, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    m_ledger->asyncGetTotalTransactionCount(
        [current](bcos::Error::Ptr _error, int64_t _totalTxCount, int64_t _failedTxCount,
            bcos::protocol::BlockNumber _latestBlockNumber) {
            async_response_asyncGetTotalTransactionCount(
                current, toTarsError(_error), _totalTxCount, _failedTxCount, _latestBlockNumber);
        });
    return bcostars::Error();
}

bcostars::Error LedgerServiceServer::asyncGetTransactionReceiptByHash(
    const vector<tars::Char>& _txHash, tars::Bool _withProof, bcostars::TransactionReceipt&,
    vector<bcostars::MerkleProofItem>&, tars::TarsCurrentPtr current)
{
    current->setResponse(false);
    bcos::crypto::HashType txHash;
    if (_txHash.size() >= bcos::crypto::HashType::SIZE)
    {
        txHash = bcos::crypto::HashType(
            reinterpret_cast<const bcos::byte*>(_txHash.data()), bcos::crypto::HashType::SIZE);
    }

    m_ledger->asyncGetTransactionReceiptByHash(txHash, _withProof,
        [current](bcos::Error::Ptr _error, bcos::protocol::TransactionReceipt::ConstPtr _receipt,
            bcos::ledger::MerkleProofPtr _merkleProofList) {
            // get tars receipt
            bcostars::TransactionReceipt tarsReceipt;
            if (_receipt)
            {
                auto mutableReceipt =
                    std::const_pointer_cast<bcos::protocol::TransactionReceipt>(_receipt);
                tarsReceipt = std::dynamic_pointer_cast<bcostars::protocol::TransactionReceiptImpl>(
                    mutableReceipt)
                                  ->inner();
            }
            // get tars merkle
            vector<bcostars::MerkleProofItem> tarsMerkleItemList;
            if (_merkleProofList)
            {
                for (auto const& merkle : *_merkleProofList)
                {
                    MerkleProofItem item;
                    item.left = merkle.first;
                    item.right = merkle.second;
                    tarsMerkleItemList.emplace_back(item);
                }
            }
            async_response_asyncGetTransactionReceiptByHash(
                current, toTarsError(_error), tarsReceipt, tarsMerkleItemList);
        });
    return bcostars::Error();
}
