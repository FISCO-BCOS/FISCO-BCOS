#pragma once

#include "bcos-concepts/Serialize.h"
#include "bcos-crypto/signature/key/KeyImpl.h"
#include "bcos-framework/consensus/ConsensusNode.h"
#include "bcos-framework/consensus/ConsensusNodeInterface.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-table/src/LegacyStorageWrapper.h"
#include "bcos-tars-protocol/impl/TarsSerializable.h"
#include "bcos-task/AwaitableValue.h"
#include "bcos-tool/ConsensusNode.h"
#include "generated/bcos-tars-protocol/tars/LedgerConfig.h"
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

task::Task<void> tag_invoke(
    ledger::tag_t<getLedgerConfig> /*unused*/, LedgerInterface& ledger, LedgerConfig& ledgerConfig);

task::Task<Features> tag_invoke(ledger::tag_t<getFeatures> /*unused*/, LedgerInterface& ledger);

task::Task<protocol::TransactionReceipt::ConstPtr> tag_invoke(
    ledger::tag_t<getReceipt>, LedgerInterface& ledger, crypto::HashType const& txHash);

task::Task<protocol::TransactionsConstPtr> tag_invoke(
    ledger::tag_t<getTransactions>, LedgerInterface& ledger, crypto::HashListPtr hashes);

task::Task<std::optional<bcos::storage::Entry>> tag_invoke(ledger::tag_t<getStorageAt>,
    LedgerInterface& ledger, std::string_view address, std::string_view key,
    bcos::protocol::BlockNumber number);

task::Task<std::shared_ptr<std::map<protocol::BlockNumber, protocol::NonceListPtr>>> tag_invoke(
    ledger::tag_t<getNonceList> /*unused*/, LedgerInterface& ledger,
    bcos::protocol::BlockNumber startNumber, int64_t offset);

task::Task<protocol::BlockNumber> tag_invoke(
    ledger::tag_t<getCurrentBlockNumber> /*unused*/, auto& storage, FromStorage /*unused*/)
{
    if (auto blockNumberEntry = co_await storage2::readOne(
            storage, transaction_executor::StateKeyView{SYS_CURRENT_STATE, SYS_KEY_CURRENT_NUMBER}))
    {
        bcos::protocol::BlockNumber blockNumber = -1;
        try
        {
            blockNumber =
                boost::lexical_cast<bcos::protocol::BlockNumber>(blockNumberEntry->getField(0));
        }
        catch (boost::bad_lexical_cast& e)
        {
            // Ignore the exception
            LEDGER_LOG(INFO) << "Cast blockNumber failed, may be empty, set to default value -1"
                             << LOG_KV("blockNumber str", blockNumberEntry->getField(0));
        }
        LEDGER_LOG(TRACE) << "GetBlockNumber success" << LOG_KV("blockNumber", blockNumber);
        co_return blockNumber;
    }
}

task::Task<consensus::ConsensusNodeList> tag_invoke(
    ledger::tag_t<getNodeList> /*unused*/, auto& storage)
{
    LEDGER_LOG(DEBUG) << "GetNodeList request";
    auto blockNumber = co_await ledger::getCurrentBlockNumber(storage, fromStorage);
    auto nodeListEntry = co_await storage2::readOne(
        storage, transaction_executor::StateKeyView{SYS_CONSENSUS, "key"});
    if (!nodeListEntry)
    {
        co_return consensus::ConsensusNodeList{};
    }

    LEDGER_LOG(DEBUG) << "Get nodeList from" << LOG_KV("blockNumber", blockNumber);
    auto ledgerConsensusNodeList = decodeConsensusList(nodeListEntry->getField(0));
    consensus::ConsensusNodeList nodes;
    nodes.reserve(ledgerConsensusNodeList.size());

    auto effectNumber = blockNumber + 1;
    for (auto&& node : RANGES::views::filter(ledgerConsensusNodeList, [&](auto const& node) {
             return boost::lexical_cast<bcos::protocol::BlockNumber>(node.enableNumber) <=
                    effectNumber;
         }))
    {
        auto nodeIDBin = fromHex(node.nodeID);
        crypto::NodeIDPtr nodeID = std::make_shared<crypto::KeyImpl>(nodeIDBin);
        uint64_t termWeight = 0;
        std::string_view extraKey{(const char*)nodeIDBin.data(), nodeIDBin.size()};
        if (auto extraEntry = co_await storage2::readOne(
                storage, transaction_executor::StateKeyView{SYS_CONSENSUS, extraKey}))
        {
            bcostars::ConsensusNode tarsConsensusNode;
            concepts::serialize::decode(extraEntry->get(), tarsConsensusNode);
            termWeight = tarsConsensusNode.termWeight;
        }

        auto type = magic_enum::enum_cast<consensus::ConsensusNodeInterface::Type>(node.type);
        if (!type)
        {
            LEDGER_LOG(ERROR) << "GetNodeListByType failed, invalid node type"
                              << LOG_KV("type", node.type);
            continue;
        }
        nodes.emplace_back(std::make_unique<consensus::ConsensusNode>(
            nodeID, *type, node.voteWeight.template convert_to<uint64_t>(), termWeight));
    }

    LEDGER_LOG(DEBUG) << "GetNodeListByType success" << LOG_KV("nodes size", nodes.size());
    co_return nodes;
}
}  // namespace bcos::ledger