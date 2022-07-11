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
 * @brief interface for Ledger
 * @file LedgerInterface.h
 * @author: kyonRay
 * @date: 2021-04-07
 */

#pragma once

#include "../protocol/Block.h"
#include "../protocol/BlockHeader.h"
#include "../protocol/Transaction.h"
#include "../protocol/TransactionReceipt.h"
#include "../storage/StorageInterface.h"
#include "LedgerConfig.h"
#include "LedgerTypeDef.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-utilities/Error.h>
#include <gsl/span>
#include <map>


namespace bcos::ledger
{
class LedgerInterface
{
public:
    using Ptr = std::shared_ptr<LedgerInterface>;
    LedgerInterface() = default;
    virtual ~LedgerInterface() {}

    /**
     * @brief async prewrite a block in scheduler module
     * @param block the block to commit
     * @param callback trigger this callback when write is finished
     */
    virtual void asyncPrewriteBlock(bcos::storage::StorageInterface::Ptr storage,
        bcos::protocol::TransactionsPtr _blockTxs, bcos::protocol::Block::ConstPtr block,
        std::function<void(Error::Ptr&&)> callback) = 0;

    /**
     * @brief async store txs in block when tx pool verify
     * @param _txToStore tx bytes data list
     * @param _txHashList tx hash list
     * @param _onTxsStored callback
     */
    virtual void asyncStoreTransactions(std::shared_ptr<std::vector<bytesConstPtr>> _txToStore,
        crypto::HashListPtr _txHashList, std::function<void(Error::Ptr)> _onTxStored) = 0;

    /**
     * @brief async get block by blockNumber
     * @param _blockNumber number of block
     * @param _blockFlag flag bit of what the block be callback contains,
     *                   you can checkout all flags in LedgerTypeDef.h
     * @param _onGetBlock
     *
     * @example
     * asyncGetBlockDataByNumber(10, HEADER|TRANSACTIONS, [](error, block){ doSomething(); });
     */
    virtual void asyncGetBlockDataByNumber(protocol::BlockNumber _blockNumber, int32_t _blockFlag,
        std::function<void(Error::Ptr, protocol::Block::Ptr)> _onGetBlock) = 0;

    /**
     * @brief async get latest block number
     * @param _onGetBlock
     */
    virtual void asyncGetBlockNumber(
        std::function<void(Error::Ptr, protocol::BlockNumber)> _onGetBlock) = 0;

    /**
     * @brief async get block hash by block number
     * @param _blockNumber the number of block to get
     * @param _onGetBlock
     */
    virtual void asyncGetBlockHashByNumber(protocol::BlockNumber _blockNumber,
        std::function<void(Error::Ptr, crypto::HashType)> _onGetBlock) = 0;

    /**
     * @brief async get block number by block hash
     * @param _blockHash the hash of block to get
     * @param _onGetBlock
     */
    virtual void asyncGetBlockNumberByHash(crypto::HashType const& _blockHash,
        std::function<void(Error::Ptr, protocol::BlockNumber)> _onGetBlock) = 0;

    /**
     * @brief async get a batch of transaction by transaction hash list
     * @param _txHashList transaction hash list, hash should be hex
     * @param _withProof if true then it will callback MerkleProofPtr map in _onGetTx
     *                   if false then MerkleProofPtr map will be nullptr
     * @param _onGetTx return <error, [tx data in bytes], map<txHash, merkleProof>
     */
    virtual void asyncGetBatchTxsByHashList(crypto::HashListPtr _txHashList, bool _withProof,
        std::function<void(Error::Ptr, bcos::protocol::TransactionsPtr,
            std::shared_ptr<std::map<std::string, MerkleProofPtr>>)>
            _onGetTx) = 0;

    /**
     * @brief async get a transaction receipt by tx hash
     * @param _txHash hash of transaction
     * @param _withProof if true then it will callback MerkleProofPtr in _onGetTx
     *                   if false then MerkleProofPtr will be nullptr
     * @param _onGetTx
     */
    virtual void asyncGetTransactionReceiptByHash(crypto::HashType const& _txHash, bool _withProof,
        std::function<void(Error::Ptr, protocol::TransactionReceipt::ConstPtr, MerkleProofPtr)>
            _onGetTx) = 0;

    /**
     * @brief async get total transaction count and latest block number
     * @param _callback callback totalTxCount, totalFailedTxCount, and latest block number
     */
    virtual void asyncGetTotalTransactionCount(std::function<void(Error::Ptr, int64_t _totalTxCount,
            int64_t _failedTxCount, protocol::BlockNumber _latestBlockNumber)>
            _callback) = 0;

    /**
     * @brief async get system config by table key
     * @param _key the key of row, you can checkout all key in LedgerTypeDef.h
     * @param _onGetConfig callback when get config, <value, latest block number>
     */
    virtual void asyncGetSystemConfigByKey(std::string_view const& _key,
        std::function<void(Error::Ptr, std::string, protocol::BlockNumber)> _onGetConfig) = 0;

    /**
     * @brief async get node list by type, can be sealer or observer
     * @param _type the type of node, CONSENSUS_SEALER or CONSENSUS_OBSERVER
     * @param _onGetConfig
     */
    virtual void asyncGetNodeListByType(std::string_view const& _type,
        std::function<void(Error::Ptr, consensus::ConsensusNodeListPtr)> _onGetConfig) = 0;

    /**
     * @brief async get a batch of nonce lists in blocks
     * @param _startNumber start block number
     * @param _offset batch offset, if batch is 0, then callback nonce list in start block number;
     * if (_startNumber + _offset) > latest block number, then callback nonce lists in
     * [_startNumber, latest number]
     * @param _onGetList
     */
    virtual void asyncGetNonceList(protocol::BlockNumber _startNumber, int64_t _offset,
        std::function<void(
            Error::Ptr, std::shared_ptr<std::map<protocol::BlockNumber, protocol::NonceListPtr>>)>
            _onGetList) = 0;

    virtual void asyncPreStoreBlockTxs(bcos::protocol::TransactionsPtr _blockTxs,
        bcos::protocol::Block::ConstPtr block,
        std::function<void(Error::UniquePtr&&)> _callback) = 0;
};
}  // namespace bcos::ledger
