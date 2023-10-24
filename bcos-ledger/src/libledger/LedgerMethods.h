#pragma once

#include "bcos-framework/consensus/ConsensusNode.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-table/src/LegacyStorageWrapper.h"
#include "bcos-table/src/StateStorageInterface.h"
#include "bcos-task/AwaitableValue.h"
#include "bcos-tool/ConsensusNode.h"
#include "bcos-tool/VersionConverter.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Error.h"
#include <boost/throw_exception.hpp>
#include <concepts>
#include <exception>
#include <iterator>
#include <type_traits>
#include <variant>

#define LEDGER2_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("LEDGER2")
namespace bcos::ledger
{

inline task::AwaitableValue<void> tag_invoke(ledger::tag_t<buildGenesisBlock> /*unused*/,
    auto& ledger, LedgerConfig::Ptr ledgerConfig, size_t gasLimit,
    const std::string_view& genesisData, std::string const& compatibilityVersion,
    bool isAuthCheck = false, std::string const& consensusType = "pbft",
    std::int64_t epochSealerNum = 4, std::int64_t epochBlockNum = 1000)
{
    ledger.buildGenesisBlock(std::move(ledgerConfig), gasLimit, genesisData, compatibilityVersion,
        isAuthCheck, consensusType, epochSealerNum, epochBlockNum);
    return {};
}

task::Task<void> prewriteBlockToStorage(LedgerInterface& ledger,
    bcos::protocol::TransactionsPtr transactions, bcos::protocol::Block::ConstPtr block,
    bool withTransactionsAndReceipts, storage::StorageInterface::Ptr storage);

inline task::Task<void> tag_invoke(ledger::tag_t<prewriteBlock> /*unused*/, LedgerInterface& ledger,
    bcos::protocol::TransactionsPtr transactions, bcos::protocol::Block::ConstPtr block,
    bool withTransactionsAndReceipts, auto&& storage)
{
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

task::Task<TransactionCount> tag_invoke(
    ledger::tag_t<getTransactionCount> /*unused*/, LedgerInterface& ledger);

task::Task<protocol::BlockNumber> tag_invoke(
    ledger::tag_t<getCurrentBlockNumber> /*unused*/, LedgerInterface& ledger);


task::Task<crypto::HashType> tag_invoke(ledger::tag_t<getBlockHash> /*unused*/,
    LedgerInterface& ledger, protocol::BlockNumber blockNumber);

task::Task<SystemConfigEntry> tag_invoke(
    ledger::tag_t<getSystemConfig> /*unused*/, LedgerInterface& ledger, std::string_view key);

task::Task<consensus::ConsensusNodeList> tag_invoke(
    ledger::tag_t<getNodeList> /*unused*/, LedgerInterface& ledger, std::string_view type);

task::Task<LedgerConfig::Ptr> tag_invoke(
    ledger::tag_t<getLedgerConfig> /*unused*/, LedgerInterface& ledger);

task::Task<Features> tag_invoke(ledger::tag_t<getFeatures> /*unused*/, LedgerInterface& ledger);
}  // namespace bcos::ledger