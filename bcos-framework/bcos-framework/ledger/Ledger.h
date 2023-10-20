#pragma once

#include "LedgerConfig.h"
#include "LedgerTypeDef.h"
#include "bcos-framework/protocol/Block.h"
#include "bcos-task/Task.h"
#include "bcos-task/Trait.h"
#include <type_traits>

namespace bcos::ledger
{
struct BuildGenesisBlock
{
    auto operator()(auto& ledger, LedgerConfig::Ptr ledgerConfig, size_t gasLimit,
        const std::string_view& genesisData, std::string const& compatibilityVersion,
        bool isAuthCheck = false, std::string const& consensusType = "pbft",
        std::int64_t epochSealerNum = 4, std::int64_t epochBlockNum = 1000) const
        -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(*this, ledger,
            std::move(ledgerConfig), gasLimit, genesisData, compatibilityVersion, isAuthCheck,
            consensusType, epochSealerNum, epochBlockNum))>>
    {
        co_await tag_invoke(*this, ledger, std::move(ledgerConfig), gasLimit, genesisData,
            compatibilityVersion, isAuthCheck, consensusType, epochSealerNum, epochBlockNum);
    }
};
inline constexpr BuildGenesisBlock buildGenesisBlock{};

struct PrewriteBlock
{
    auto operator()(auto& ledger, bcos::protocol::TransactionsPtr transactions,
        bcos::protocol::Block::ConstPtr block, bool withTransactionsAndReceipts,
        auto& storage) const -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(*this,
        ledger, transactions, block, storage))>>
    {
        co_await tag_invoke(*this, ledger, std::move(transactions), std::move(block),
            withTransactionsAndReceipts, storage);
    }
};
inline constexpr PrewriteBlock prewriteBlock{};

struct TransactionCount
{
    int64_t total;
    int64_t failed;
    bcos::protocol::BlockNumber blockNumber;
};
struct GetTransactionCount
{
    auto operator()(auto& ledger) const
        -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(*this, ledger))>>
        requires std::same_as<task::AwaitableReturnType<decltype(tag_invoke(*this, ledger))>,
            TransactionCount>
    {
        co_return co_await tag_invoke(*this, ledger);
    }
};
inline constexpr GetTransactionCount getTransactionCount{};

using SystemConfigEntry = std::tuple<std::string, protocol::BlockNumber>;
struct GetSystemConfig
{
    auto operator()(auto& ledger, std::string_view key) const
        -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(*this, ledger, key))>>
    {
        co_return co_await tag_invoke(*this, ledger, key);
    }
};
inline constexpr GetSystemConfig getSystemConfig{};

struct GetNodeList
{
    auto operator()(auto& ledger, std::string_view type) const
        -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(*this, ledger, type))>>
    {
        co_return co_await tag_invoke(*this, ledger, type);
    }
};
inline constexpr GetNodeList getNodeList{};

struct GetLedgerConfig
{
    auto operator()(auto& ledger) const
        -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(*this, ledger))>>
        requires std::same_as<task::AwaitableReturnType<decltype(tag_invoke(*this, ledger))>,
            LedgerConfig::Ptr>
    {
        co_return co_await tag_invoke(*this, ledger);
    }
};
inline constexpr GetLedgerConfig getLedgerConfig{};

template <auto& Tag>
using tag_t = std::decay_t<decltype(Tag)>;
}  // namespace bcos::ledger