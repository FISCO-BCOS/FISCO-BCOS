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

    bcos::task::Task<void> preStoreBlockTxs(
        bcos::protocol::ConstTransactionsPtr, bcos::protocol::Block::ConstPtr) override;

    // TODO: implement this
    bcos::task::Task<std::string> prewriteBlock(bcos::storage::StorageInterface::Ptr,
        bcos::protocol::ConstTransactionsPtr, bcos::protocol::Block::ConstPtr, bool,
        std::optional<bcos::crypto::HashType>, bool writeNonces) override;

    // TODO: implement this
    bcos::Error::Ptr storeTransactionsAndReceipts(
        bcos::protocol::ConstTransactionsPtr, bcos::protocol::Block::ConstPtr) override;

    bcos::task::Task<bcos::protocol::Block::Ptr> getBlockDataByNumber(
        bcos::protocol::BlockNumber blockNumber, int32_t blockFlag) override;

    bcos::task::Task<bcos::protocol::BlockNumber> getBlockNumber() override;

    bcos::task::Task<bcos::crypto::HashType> getBlockHashByNumber(
        bcos::protocol::BlockNumber blockNumber) override;

    bcos::task::Task<bcos::protocol::BlockNumber> getBlockNumberByHash(
        bcos::crypto::HashType blockHash) override;

    bcos::task::Task<bcos::ledger::BatchTransactionsResult> getBatchTxsByHashList(
        bcos::crypto::HashListPtr txHashList, bool withProof) override;

    bcos::task::Task<bcos::ledger::TransactionReceiptResult> getTransactionReceiptByHash(
        bcos::crypto::HashType txHash, bool withProof) override;

    bcos::task::Task<bcos::ledger::TransactionCountResult> getTotalTransactionCount() override;

    bcos::task::Task<std::optional<bcos::storage::Entry>> getCurrentStateByKey(
        std::string_view key) override;

    bcos::Error::Ptr setCurrentStateByKey(
        std::string_view const& _key, bcos::storage::Entry entry) override;

    bcos::task::Task<bcos::ledger::SystemConfigEntry> getSystemConfigByKey(
        std::string_view key) override;

    bcos::task::Task<bcos::consensus::ConsensusNodeList> getNodeListByType(
        std::string_view type) override;

    bcos::task::Task<bcos::ledger::NonceListResult> getNonceList(
        bcos::protocol::BlockNumber startNumber, int64_t offset) override;

    void removeExpiredNonce(bcos::protocol::BlockNumber blockNumber, bool sync) override;

private:
    void asyncGetBlockDataByNumber(bcos::protocol::BlockNumber _blockNumber, int32_t _blockFlag,
        std::function<void(bcos::Error::Ptr, bcos::protocol::Block::Ptr)> _onGetBlock);

    void asyncGetBlockNumber(
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockNumber)> _onGetBlock);

    void asyncGetBlockHashByNumber(bcos::protocol::BlockNumber _blockNumber,
        std::function<void(bcos::Error::Ptr, bcos::crypto::HashType)> _onGetBlock);

    void asyncGetBlockNumberByHash(bcos::crypto::HashType const& _blockHash,
        std::function<void(bcos::Error::Ptr, bcos::protocol::BlockNumber)> _onGetBlock);

    void asyncGetBatchTxsByHashList(bcos::crypto::HashListPtr _txHashList, bool _withProof,
        std::function<void(bcos::Error::Ptr, bcos::protocol::TransactionsPtr,
            std::shared_ptr<std::map<std::string, bcos::ledger::MerkleProofPtr>>)>
            _onGetTx);

    void asyncGetTransactionReceiptByHash(bcos::crypto::HashType const& _txHash, bool _withProof,
        std::function<void(bcos::Error::Ptr, bcos::protocol::TransactionReceipt::Ptr,
            bcos::ledger::MerkleProofPtr)>
            _onGetTx);

    void asyncGetTotalTransactionCount(std::function<void(bcos::Error::Ptr, int64_t _totalTxCount,
            int64_t _failedTxCount, bcos::protocol::BlockNumber _latestBlockNumber)>
            _callback);

    void asyncGetSystemConfigByKey(std::string_view const& _key,
        std::function<void(bcos::Error::Ptr, std::string, bcos::protocol::BlockNumber)>
            _onGetConfig);

    void asyncGetNodeListByType(std::string_view const& _type,
        std::function<void(bcos::Error::Ptr, bcos::consensus::ConsensusNodeList)> _onGetConfig);

private:
    bcostars::LedgerServicePrx m_prx;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    bcos::crypto::CryptoSuite::Ptr m_cryptoSuite;
    bcos::crypto::KeyFactory::Ptr m_keyFactory;
};
}  // namespace bcostars
