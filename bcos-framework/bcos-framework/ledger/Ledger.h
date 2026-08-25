#pragma once

#include "GenesisConfig.h"
#include "LedgerConfig.h"
#include "LedgerTypeDef.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Block.h"
#include "bcos-framework/protocol/BlockFactory.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-task/Task.h"
#include <range/v3/range/concepts.hpp>
#include <bcos-utilities/BoostLog.h>

#define LEDGER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("LEDGER")

namespace bcos::ledger
{

inline constexpr struct FromStorage
{
} fromStorage{};

inline constexpr struct BuildGenesisBlock
{
    task::Task<bool> operator()(
        auto& ledger, GenesisConfig const& genesis, ledger::LedgerConfig const& ledgerConfig) const
    {
        co_return co_await tag_invoke(*this, ledger, genesis, ledgerConfig);
    }
} buildGenesisBlock{};

inline constexpr struct PrewriteBlock
{
    task::Task<void> operator()(auto& ledger, bcos::protocol::ConstTransactionsPtr transactions,
        bcos::protocol::Block::ConstPtr block, bool withTransactionsAndReceipts, auto& storage,
        std::optional<bcos::crypto::HashType> blockHashOverride = std::nullopt,
        bool writeNonces = true) const
    {
        co_await tag_invoke(*this, ledger, std::move(transactions), std::move(block),
            withTransactionsAndReceipts, storage, blockHashOverride, writeNonces);
    }
} prewriteBlock{};

// FIB-104: Routes the entire block (header, hash mappings, nonces, current number,
// tx metadata, transactions, receipts) into the caller-provided storage buffer so
// that a subsequent mergeBack/commit can persist all of it atomically.
//
// NOTE: This bypasses the m_blockStorage / m_stateStorage separation honored by
// PrewriteBlock; transactions and receipts always go to the passed `storage`.
// Only suitable for single-backend deployments (e.g. AIR mode). Archive-mode
// callers that require a separate block-data backend must continue to use
// PrewriteBlock + StoreTransactionsAndReceipts.
inline constexpr struct PrewriteBlockToBuffer
{
    task::Task<void> operator()(auto& ledger, bcos::protocol::ConstTransactionsPtr transactions,
        bcos::protocol::Block::ConstPtr block, auto& storage,
        std::optional<bcos::crypto::HashType> blockHashOverride = std::nullopt,
        bool writeNonces = true) const
    {
        co_await tag_invoke(*this, ledger, std::move(transactions), std::move(block), storage,
            blockHashOverride, writeNonces);
    }
} prewriteBlockToBuffer{};

inline constexpr struct StoreTransactionsAndReceipts
{
    task::Task<void> operator()(auto& ledger, bcos::protocol::ConstTransactionsPtr transactions,
        bcos::protocol::Block::ConstPtr block) const
    {
        co_await tag_invoke(*this, ledger, std::move(transactions), std::move(block));
    }
} storeTransactionsAndReceipts{};

inline constexpr struct RemoveExpiredNonce
{
    void operator()(auto& ledger, protocol::BlockNumber expiredNumber) const
    {
        tag_invoke(*this, ledger, expiredNumber);
    }
} removeExpiredNonce{};

inline constexpr struct GetBlockData
{
    task::Task<protocol::Block::Ptr> operator()(
        auto& ledger, protocol::BlockNumber blockNumber, int32_t blockFlag) const
    {
        co_return co_await tag_invoke(*this, ledger, blockNumber, blockFlag);
    }
    task::Task<protocol::Block::Ptr> operator()(auto& storage, protocol::BlockNumber blockNumber,
        int32_t blockFlag, protocol::BlockFactory& blockFactory) const
    {
        co_return co_await tag_invoke(*this, storage, blockNumber, blockFlag, blockFactory);
    }
} getBlockData{};

struct TransactionCount
{
    int64_t total{};
    int64_t failed{};
    bcos::protocol::BlockNumber blockNumber{};
};
inline constexpr struct GetTransactionCount
{
    task::Task<TransactionCount> operator()(auto& ledger) const
    {
        co_return co_await tag_invoke(*this, ledger);
    }
} getTransactionCount{};

inline constexpr struct GetCurrentBlockNumber
{
    task::Task<protocol::BlockNumber> operator()(auto& ledger) const
    {
        co_return co_await tag_invoke(*this, ledger);
    }
    task::Task<protocol::BlockNumber> operator()(
        storage2::ReadableStorage<executor_v1::StateKey> auto& storage,
        FromStorage fromStorage) const
    {
        co_return co_await tag_invoke(*this, storage, fromStorage);
    }
} getCurrentBlockNumber{};

inline constexpr struct GetBlockHash
{
    task::Task<crypto::HashType> operator()(auto& ledger, protocol::BlockNumber blockNumber) const
    {
        co_return co_await tag_invoke(*this, ledger, blockNumber);
    }
    task::Task<std::optional<crypto::HashType>> operator()(
        storage2::ReadableStorage<executor_v1::StateKey> auto& storage,
        protocol::BlockNumber blockNumber, FromStorage fromStorage) const
    {
        co_return co_await tag_invoke(*this, storage, blockNumber, fromStorage);
    }
} getBlockHash{};

inline constexpr struct GetBlockNumber
{
    task::Task<protocol::BlockNumber> operator()(auto& ledger, crypto::HashType hash) const
    {
        co_return co_await tag_invoke(*this, ledger, hash);
    }
    task::Task<std::optional<protocol::BlockNumber>> operator()(
        storage2::ReadableStorage<executor_v1::StateKey> auto& storage, crypto::HashType hash,
        FromStorage fromStorage) const
    {
        co_return co_await tag_invoke(*this, storage, hash, fromStorage);
    }
} getBlockNumber{};

using SystemConfigEntry = std::tuple<std::string, protocol::BlockNumber>;
inline constexpr struct GetSystemConfig
{
    task::Task<std::optional<SystemConfigEntry>> operator()(
        auto& ledger, std::string_view key) const
    {
        co_return co_await tag_invoke(*this, ledger, key);
    }
    task::Task<std::optional<SystemConfigEntry>> operator()(
        storage2::ReadableStorage<executor_v1::StateKey> auto& storage, std::string_view key,
        FromStorage /*unused*/) const
    {
        co_return co_await tag_invoke(*this, storage, key);
    }
} getSystemConfig{};

inline constexpr struct GetNodeList
{
    task::Task<consensus::ConsensusNodeList> operator()(auto& ledger, std::string_view type) const
    {
        co_return co_await tag_invoke(*this, ledger, type);
    }

    task::Task<consensus::ConsensusNodeList> operator()(auto& storage) const
    {
        co_return co_await tag_invoke(*this, storage);
    }
} getNodeList{};

inline constexpr struct SetNodeList
{
    task::Task<void> operator()(
        storage2::WritableStorage<executor_v1::StateKey, executor_v1::StateValue> auto& storage,
        ::ranges::input_range auto&& nodeList, auto&&... args) const
    {
        co_await tag_invoke(*this, storage, std::forward<decltype(nodeList)>(nodeList),
            std::forward<decltype(args)>(args)...);
    }
} setNodeList{};

inline constexpr struct GetLedgerConfig
{
    task::Task<void> operator()(auto& ledger, LedgerConfig& ledgerConfig) const
    {
        co_await tag_invoke(*this, ledger, ledgerConfig);
    }
    task::Task<LedgerConfig::Ptr> operator()(auto& ledger) const
    {
        auto ledgerConfig = std::make_shared<LedgerConfig>();
        co_await tag_invoke(*this, ledger, *ledgerConfig);
        co_return ledgerConfig;
    }

    // Read from storage
    task::Task<void> operator()(storage2::ReadableStorage<executor_v1::StateKey> auto& storage,
        LedgerConfig& ledgerConfig, protocol::BlockNumber blockNumber,
        protocol::BlockFactory& blockFactory) const
    {
        co_await tag_invoke(*this, storage, ledgerConfig, blockNumber, blockFactory);
    }
    task::Task<LedgerConfig::Ptr> operator()(
        storage2::ReadableStorage<executor_v1::StateKey> auto& storage,
        protocol::BlockNumber blockNumber, protocol::BlockFactory& blockFactory) const
    {
        auto ledgerConfig = std::make_shared<LedgerConfig>();
        co_await tag_invoke(*this, storage, *ledgerConfig, blockNumber, blockFactory);
        co_return ledgerConfig;
    }
} getLedgerConfig{};

inline constexpr struct GetFeatures
{
    task::Task<Features> operator()(auto& ledger) const
    {
        co_return co_await tag_invoke(*this, ledger);
    }
} getFeatures{};

inline constexpr struct GetFeature
{
    /// Read ONE feature flag's enabled state at @p blockNumber (single SYS_CONFIG read,
    /// vs getFeatures' read of every flag). Degrades to false on any read failure, the
    /// same honest scenario-A default as getFeatures' empty-set fallback.
    task::Task<bool> operator()(auto& ledger, Features::Flag flag,
        protocol::BlockNumber blockNumber) const
    {
        co_return co_await tag_invoke(*this, ledger, flag, blockNumber);
    }
} getFeature{};

inline constexpr struct GetReceipt
{
    task::Task<protocol::TransactionReceipt::Ptr> operator()(
        auto& ledger, crypto::HashType hash) const
    {
        co_return co_await tag_invoke(*this, ledger, hash);
    }
} getReceipt{};

inline constexpr struct GetTransactions
{
    task::Task<protocol::TransactionsConstPtr> operator()(
        auto& ledger, crypto::HashListPtr hashes) const
    {
        co_return co_await tag_invoke(*this, ledger, std::move(hashes));
    }
} getTransactions{};

inline constexpr struct GetStorageAt
{
    task::Task<std::optional<bcos::storage::Entry>> operator()(auto& ledger,
        std::string_view address, std::string_view key, bcos::protocol::BlockNumber number) const
    {
        co_return co_await tag_invoke(*this, ledger, address, key, number);
    }
} getStorageAt{};

inline constexpr struct GetNonceList
{
    task::Task<std::shared_ptr<std::map<protocol::BlockNumber, protocol::NonceListPtr>>> operator()(
        auto& ledger, bcos::protocol::BlockNumber startNumber, int64_t offset) const
    {
        co_return co_await tag_invoke(*this, ledger, startNumber, offset);
    }
} getNonceList{};

template <auto& Tag>
using tag_t = std::decay_t<decltype(Tag)>;
}  // namespace bcos::ledger
