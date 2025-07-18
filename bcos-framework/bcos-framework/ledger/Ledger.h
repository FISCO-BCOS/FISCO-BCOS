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
        bcos::protocol::Block::ConstPtr block, bool withTransactionsAndReceipts,
        auto& storage) const
    {
        co_await tag_invoke(*this, ledger, std::move(transactions), std::move(block),
            withTransactionsAndReceipts, storage);
    }
} prewriteBlock{};

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
        int32_t blockFlag, protocol::BlockFactory& blockFactory, FromStorage fromStorage) const
    {
        co_return co_await tag_invoke(
            *this, storage, blockNumber, blockFlag, blockFactory, fromStorage);
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
        RANGES::input_range auto&& nodeList, auto&&... args) const
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
        LedgerConfig& ledgerConfig, protocol::BlockNumber blockNumber) const
    {
        co_await tag_invoke(*this, storage, ledgerConfig, blockNumber);
    }
    task::Task<LedgerConfig::Ptr> operator()(
        storage2::ReadableStorage<executor_v1::StateKey> auto& storage,
        protocol::BlockNumber blockNumber) const
    {
        auto ledgerConfig = std::make_shared<LedgerConfig>();
        co_await tag_invoke(*this, storage, *ledgerConfig, blockNumber);
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

inline constexpr struct GetReceipt
{
    task::Task<protocol::TransactionReceipt::ConstPtr> operator()(
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
