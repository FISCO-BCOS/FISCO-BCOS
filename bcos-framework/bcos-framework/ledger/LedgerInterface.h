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
#include "../protocol/Transaction.h"
#include "../protocol/TransactionReceipt.h"
#include "../storage/StorageInterface.h"
#include "Features.h"
#include "LedgerTypeDef.h"
#include "SystemConfigs.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-task/Task.h>
#include <bcos-utilities/Error.h>
#include <boost/lexical_cast.hpp>
#include <gsl/span>
#include <map>
#include <tuple>
#include <utility>


namespace bcos::ledger
{
using MerkleProofMapPtr = std::shared_ptr<std::map<std::string, MerkleProofPtr>>;
using BatchTransactionsResult = std::pair<protocol::TransactionsPtr, MerkleProofMapPtr>;
using TransactionCountResult = std::tuple<int64_t, int64_t, protocol::BlockNumber>;
using NonceListResult = std::shared_ptr<std::map<protocol::BlockNumber, protocol::NonceListPtr>>;

struct TransactionReceiptResult
{
    protocol::TransactionReceipt::Ptr receipt;
    MerkleProofPtr proof;
    // Only a proof failure can coexist with a receipt. Read failures are thrown.
    Error::Ptr error;
};

class LedgerInterface
{
public:
    using Ptr = std::shared_ptr<LedgerInterface>;
    LedgerInterface() = default;
    virtual ~LedgerInterface() = default;

    virtual task::Task<void> preStoreBlockTxs(
        protocol::ConstTransactionsPtr blockTxs, protocol::Block::ConstPtr block) = 0;

    /**
     * @brief prewrite a block in scheduler module
     * @param block the block to commit
     * @return the primary key of the hash-to-number row; empty for the system-contract-deploy
     * block, where only SYS_KEY_CURRENT_NUMBER is written
     */
    // Default arguments live ONLY on this interface declaration. Virtual default arguments
    // resolve by the STATIC type at the call site and the compiler does not check that an
    // override's defaults agree — duplicating them per-override lets a later edit to one
    // silently change behaviour depending on which pointer type the caller holds. Callers
    // invoking through a concrete class must pass every argument explicitly.
    virtual task::Task<std::string> prewriteBlock(bcos::storage::StorageInterface::Ptr storage,
        bcos::protocol::ConstTransactionsPtr _blockTxs, bcos::protocol::Block::ConstPtr block,
        bool writeTxsAndReceipts = true,
        std::optional<bcos::crypto::HashType> blockHashOverride = std::nullopt,
        bool writeNonces = true) = 0;

    virtual bcos::Error::Ptr storeTransactionsAndReceipts(
        bcos::protocol::ConstTransactionsPtr blockTxs, bcos::protocol::Block::ConstPtr block) = 0;

    virtual task::Task<protocol::Block::Ptr> getBlockDataByNumber(
        protocol::BlockNumber blockNumber, int32_t blockFlag) = 0;

    virtual task::Task<protocol::BlockNumber> getBlockNumber() = 0;

    virtual task::Task<crypto::HashType> getBlockHashByNumber(
        protocol::BlockNumber blockNumber) = 0;

    virtual task::Task<protocol::BlockNumber> getBlockNumberByHash(crypto::HashType blockHash) = 0;

    virtual task::Task<BatchTransactionsResult> getBatchTxsByHashList(
        crypto::HashListPtr txHashList, bool withProof) = 0;

    virtual task::Task<TransactionReceiptResult> getTransactionReceiptByHash(
        crypto::HashType txHash, bool withProof) = 0;

    virtual task::Task<TransactionCountResult> getTotalTransactionCount() = 0;

    virtual task::Task<std::optional<storage::Entry>> getCurrentStateByKey(
        std::string_view key) = 0;

    virtual Error::Ptr setCurrentStateByKey(
        std::string_view const& _key, bcos::storage::Entry entry)
    {
        return nullptr;
    }

    virtual task::Task<SystemConfigEntry> getSystemConfigByKey(std::string_view key) = 0;

    virtual task::Task<consensus::ConsensusNodeList> getNodeListByType(std::string_view type) = 0;

    virtual task::Task<NonceListResult> getNonceList(
        protocol::BlockNumber startNumber, int64_t offset) = 0;

    virtual void removeExpiredNonce(protocol::BlockNumber blockNumber, bool sync) = 0;

    /**
     * @brief get storage value by address and key and block number in coroutine. It will access the
     * table of address like /apps/[address] and get the value of key. NOTE: blockNumber is ignored
     * nowadays, it will always get the latest value of key in address.
     * @param _address the address of contract/EOA. if in EVM, it should be the address of contract,
     * hex string.
     * @param _key the key of storage
     * @param _blockNumber the block number to get the storage value
     * @return the storage value of key in address
     */
    virtual task::Task<std::optional<storage::Entry>> getStorageAt(
        std::string_view _address, std::string_view _key, protocol::BlockNumber _blockNumber)
    {
        co_return std::nullopt;
    }

    virtual task::Task<std::optional<ledger::StorageState>> getStorageState(
        std::string_view _address, protocol::BlockNumber _blockNumber)
    {
        co_return std::nullopt;
    }

    virtual task::Task<bcos::ledger::SystemConfigs> fetchAllSystemConfigs(
        protocol::BlockNumber _blockNumber)
    {
        co_return bcos::ledger::SystemConfigs{};
    }

    virtual task::Task<bcos::ledger::Features> fetchAllFeatures(protocol::BlockNumber _blockNumber)
    {
        co_return bcos::ledger::Features{};
    }

    /// Read ONE feature flag's enabled state at @p _blockNumber. The default implementation
    /// derives it from fetchAllFeatures (correct but reads every feature key); production
    /// Ledger overrides with a single SYS_CONFIG read (round-2 Finding E: the historical
    /// state-read path needs exactly one flag (e.g. feature_mpt_state_root), not a ~60-key scan).
    virtual task::Task<bool> fetchFeature(
        bcos::ledger::Features::Flag _flag, protocol::BlockNumber _blockNumber)
    {
        auto features = co_await fetchAllFeatures(_blockNumber);
        co_return features.get(_flag);
    }

    /// Read the executor_version SYS_CONFIG entry effective at @p _blockNumber (0 when the
    /// row is absent or not yet enabled — a pre-Ethereum-lane chain). The default derives it
    /// from fetchAllSystemConfigs (correct but reads every config key); production Ledger
    /// overrides with a single SYS_CONFIG row read (the historical state-read path's lane
    /// decision, same Finding-E shape as fetchFeature). The lane it decides is genesis-fixed:
    /// SystemConfigPrecompiled refuses governance writes crossing ETHEREUM_EXECUTOR_VERSION,
    /// so any block number resolves it correctly.
    virtual task::Task<int64_t> fetchExecutorVersionAt(protocol::BlockNumber _blockNumber)
    {
        auto configs = co_await fetchAllSystemConfigs(_blockNumber);
        if (auto entry = configs.get(ledger::SystemConfig::executor_version))
        {
            try
            {
                co_return boost::lexical_cast<int64_t>(entry->first);
            }
            catch (boost::bad_lexical_cast const&)
            {
                co_return 0;
            }
        }
        co_return 0;
    }

    virtual bcos::storage::StorageInterface::Ptr getStateStorage() { return nullptr; }
};

}  // namespace bcos::ledger
