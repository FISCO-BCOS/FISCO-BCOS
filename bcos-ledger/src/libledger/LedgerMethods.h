#pragma once

#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-table/src/LegacyStorageWrapper.h"
#include "bcos-task/AwaitableValue.h"
#include <boost/throw_exception.hpp>
#include <concepts>
#include <type_traits>

#define LEDGER2_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("LEDGER2")
namespace bcos::ledger
{

inline task::AwaitableValue<bool> tag_invoke(ledger::tag_t<buildGenesisBlock> /*unused*/,
    auto& ledger, GenesisConfig const& genesis, ledger::LedgerConfig const& ledgerConfig)
{
    return {ledger.buildGenesisBlock(genesis, ledgerConfig)};
}

task::Task<void> prewriteBlockToStorage(LedgerInterface& ledger,
    bcos::protocol::ConstTransactionsPtr transactions, bcos::protocol::Block::ConstPtr block,
    bool withTransactionsAndReceipts, storage::StorageInterface::Ptr storage);

inline task::Task<void> tag_invoke(ledger::tag_t<prewriteBlock> /*unused*/, LedgerInterface& ledger,
    bcos::protocol::ConstTransactionsPtr transactions, bcos::protocol::Block::ConstPtr block,
    bool withTransactionsAndReceipts, auto& storage)
{
    static_assert(
        !std::convertible_to<std::remove_const_t<decltype(storage)>, storage::StorageInterface&>,
        "Please use StorageInterface::Ptr");

    storage::StorageInterface::Ptr legacyStorage;
    if constexpr (std::convertible_to<std::decay_t<decltype(storage)>,
                      storage::StorageInterface::Ptr>)
    {
        legacyStorage = storage;
    }
    else
    {
        legacyStorage =
            std::make_shared<storage::LegacyStorageWrapper<std::decay_t<decltype(storage)>>>(
                storage);
    }

    co_return co_await prewriteBlockToStorage(ledger, std::move(transactions), std::move(block),
        withTransactionsAndReceipts, std::move(legacyStorage));
}

task::Task<void> tag_invoke(ledger::tag_t<storeTransactionsAndReceipts>, LedgerInterface& ledger,
    bcos::protocol::ConstTransactionsPtr blockTxs, bcos::protocol::Block::ConstPtr block);

task::Task<void> tag_invoke(ledger::tag_t<removeExpiredNonce>, LedgerInterface& ledger,
    protocol::BlockNumber expiredNumber);

task::Task<protocol::Block::Ptr> tag_invoke(ledger::tag_t<getBlockData> /*unused*/,
    LedgerInterface& ledger, protocol::BlockNumber blockNumber, int32_t blockFlag);

task::Task<TransactionCount> tag_invoke(
    ledger::tag_t<getTransactionCount> /*unused*/, LedgerInterface& ledger);

task::Task<protocol::BlockNumber> tag_invoke(
    ledger::tag_t<getCurrentBlockNumber> /*unused*/, LedgerInterface& ledger);

task::Task<crypto::HashType> tag_invoke(ledger::tag_t<getBlockHash> /*unused*/,
    LedgerInterface& ledger, protocol::BlockNumber blockNumber);

task::Task<protocol::BlockNumber> tag_invoke(
    ledger::tag_t<getBlockNumber> /*unused*/, LedgerInterface& ledger, crypto::HashType hash);

task::Task<std::optional<SystemConfigEntry>> tag_invoke(
    ledger::tag_t<getSystemConfig> /*unused*/, LedgerInterface& ledger, std::string_view key);

task::Task<consensus::ConsensusNodeList> tag_invoke(
    ledger::tag_t<getNodeList> /*unused*/, LedgerInterface& ledger, std::string_view type);

task::Task<LedgerConfig::Ptr> tag_invoke(
    ledger::tag_t<getLedgerConfig> /*unused*/, LedgerInterface& ledger);

task::Task<Features> tag_invoke(ledger::tag_t<getFeatures> /*unused*/, LedgerInterface& ledger);

task::Task<protocol::TransactionReceipt::ConstPtr> tag_invoke(
    ledger::tag_t<getReceipt>, LedgerInterface& ledger, crypto::HashType const& txHash);

task::Task<protocol::TransactionsConstPtr> tag_invoke(
    ledger::tag_t<getTransactions>, LedgerInterface& ledger, crypto::HashListPtr hashes);

task::Task<std::optional<bcos::storage::Entry>> tag_invoke(ledger::tag_t<getStorageAt>,
    LedgerInterface& ledger, std::string_view address, std::string_view key,
    bcos::protocol::BlockNumber number);

}  // namespace bcos::ledger