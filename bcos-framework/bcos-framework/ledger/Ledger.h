#pragma once

#include "GenesisConfig.h"
#include "LedgerConfig.h"
#include "LedgerTypeDef.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Block.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-task/Task.h"
#include <type_traits>

namespace bcos::ledger
{
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
    task::Task<void> operator()(auto& ledger, protocol::BlockNumber expiredNumber) const
    {
        co_await tag_invoke(*this, ledger, expiredNumber);
    }
} removeExpiredNonce{};

inline constexpr struct GetBlockData
{
    task::Task<protocol::Block::Ptr> operator()(
        auto& ledger, protocol::BlockNumber blockNumber, int32_t blockFlag) const
    {
        co_return co_await tag_invoke(*this, ledger, blockNumber, blockFlag);
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
} getCurrentBlockNumber{};

inline constexpr struct GetBlockHash
{
    task::Task<crypto::HashType> operator()(auto& ledger, protocol::BlockNumber blockNumber) const
    {
        co_return co_await tag_invoke(*this, ledger, blockNumber);
    }
} getBlockHash{};

inline constexpr struct GetBlockNumber
{
    task::Task<protocol::BlockNumber> operator()(auto& ledger, crypto::HashType hash) const
    {
        co_return co_await tag_invoke(*this, ledger, std::move(hash));
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
} getSystemConfig{};

inline constexpr struct GetNodeList
{
    task::Task<consensus::ConsensusNodeList> operator()(auto& ledger, std::string_view type) const
    {
        co_return co_await tag_invoke(*this, ledger, type);
    }
} getNodeList{};

inline constexpr struct GetLedgerConfig
{
    task::Task<LedgerConfig::Ptr> operator()(auto& ledger) const
    {
        co_return co_await tag_invoke(*this, ledger);
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

template <auto& Tag>
using tag_t = std::decay_t<decltype(Tag)>;
}  // namespace bcos::ledger
