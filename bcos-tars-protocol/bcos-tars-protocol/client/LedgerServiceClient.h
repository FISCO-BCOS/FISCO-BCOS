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
 * @file LedgerServiceClient.h
 * @author: yujiechen
 * @date 2021-10-17
 */
#pragma once

#include <bcos-framework/ledger/LedgerInterface.h>
#include <bcos-framework/protocol/BlockFactory.h>
#include <bcos-tars-protocol/tars/LedgerService.h>
#include <bcos-utilities/Common.h>
namespace bcostars
{
class LedgerServiceClient : public bcos::ledger::LedgerInterface
{
public:
    LedgerServiceClient(
        bcostars::LedgerServicePrx _prx, bcos::protocol::BlockFactory::Ptr _blockFactory);
    ~LedgerServiceClient() override;

    // TODO: implement this
    void asyncPrewriteBlock(bcos::storage::StorageInterface::Ptr,
        bcos::protocol::ConstTransactionsPtr, bcos::protocol::Block::ConstPtr,
        std::function<void(std::string, bcos::Error::Ptr&&)>, bool,
        std::optional<bcos::ledger::Features>) override;

    void asyncPreStoreBlockTxs(bcos::protocol::ConstTransactionsPtr,
        bcos::protocol::Block::ConstPtr, std::function<void(bcos::Error::UniquePtr&&)>) override;

    // TODO: implement this
    bcos::Error::Ptr storeTransactionsAndReceipts(
        bcos::protocol::ConstTransactionsPtr, bcos::protocol::Block::ConstPtr) override;

    void asyncGetBlockDataByNumber(bcos::protocol::BlockNumber _blockNumber, int32_t _blockFlag,
        std::function<void(bcos::Error::Ptr, bcos::protocol::Block::Ptr)> _onGetBlock) override;

    void asyncGetBlockNumber(
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockNumber)> _onGetBlock) override;

    void asyncGetBlockHashByNumber(bcos::protocol::BlockNumber _blockNumber,
        std::function<void(bcos::Error::Ptr, bcos::crypto::HashType)> _onGetBlock) override;

    void asyncGetBlockNumberByHash(bcos::crypto::HashType const& _blockHash,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockNumber)> _onGetBlock) override;

    void asyncGetBatchTxsByHashList(bcos::crypto::HashListPtr _txHashList, bool _withProof,
        std::function<void(bcos::Error::Ptr, bcos::protocol::TransactionsPtr,
            std::shared_ptr<std::map<std::string, bcos::ledger::MerkleProofPtr>>)>
            _onGetTx) override;

    void asyncGetTransactionReceiptByHash(bcos::crypto::HashType const& _txHash, bool _withProof,
        std::function<void(bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr,
            bcos::ledger::MerkleProofPtr)>
            _onGetTx) override;

    void asyncGetTotalTransactionCount(std::function<void(bcos::Error::Ptr, int64_t _totalTxCount,
            int64_t _failedTxCount, bcos::protocol::BlockNumber _latestBlockNumber)>
            _callback) override;

    virtual void asyncGetCurrentStateByKey(std::string_view const& _key,
        std::function<void(bcos::Error::Ptr&&, std::optional<bcos::storage::Entry>&&)> _callback)
        override;

    bcos::Error::Ptr setCurrentStateByKey(
        std::string_view const& _key, bcos::storage::Entry entry) override;

    void asyncGetSystemConfigByKey(std::string_view const& _key,
        std::function<void(bcos::Error::Ptr, std::string, bcos::protocol::BlockNumber)>
            _onGetConfig) override;

    void asyncGetNodeListByType(std::string_view const& _type,
        std::function<void(bcos::Error::Ptr, bcos::consensus::ConsensusNodeList)> _onGetConfig)
        override;

    // TODO: implement this
    void asyncGetNonceList(bcos::protocol::BlockNumber, int64_t,
        std::function<void(bcos::Error::Ptr,
            std::shared_ptr<std::map<bcos::protocol::BlockNumber, bcos::protocol::NonceListPtr>>)>)
        override;

    void removeExpiredNonce(bcos::protocol::BlockNumber blockNumber, bool sync) override;

private:
    bcostars::LedgerServicePrx m_prx;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    bcos::crypto::KeyFactory::Ptr m_keyFactory;
};
}  // namespace bcostars
