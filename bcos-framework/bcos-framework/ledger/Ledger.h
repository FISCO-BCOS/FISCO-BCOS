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
        bcos::protocol::Block::ConstPtr block, bool withTransactionsAndReceipts, auto& storage)
        -> task::Task<task::AwaitableReturnType<decltype(tag_invoke(
            *this, ledger, transactions, block, storage))>>
    {
        co_await tag_invoke(*this, ledger, std::move(transactions), std::move(block),
            withTransactionsAndReceipts, storage);
    }
};
inline constexpr PrewriteBlock prewriteBlock{};

template <auto& Tag>
using tag_t = std::decay_t<decltype(Tag)>;
}  // namespace bcos::ledger