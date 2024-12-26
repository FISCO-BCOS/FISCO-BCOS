#pragma once

#include "ConsensusNode.h"
#include "bcos-concepts/Serialize.h"
#include "bcos-crypto/signature/key/KeyImpl.h"
#include "bcos-framework/consensus/ConsensusNode.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-table/src/LegacyStorageWrapper.h"
#include "bcos-tars-protocol/impl/TarsSerializable.h"
#include "bcos-task/AwaitableValue.h"
#include "bcos-utilities/Exceptions.h"
#include "generated/bcos-tars-protocol/tars/LedgerConfig.h"
#include <boost/lexical_cast.hpp>
#include <boost/throw_exception.hpp>
#include <concepts>
#include <optional>
#include <type_traits>

#define LEDGER2_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("LEDGER2")
namespace bcos::ledger
{

struct InvalidConsensusType : public bcos::Exception
{
};

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

void tag_invoke(ledger::tag_t<removeExpiredNonce>, LedgerInterface& ledger,
    protocol::BlockNumber expiredNumber);

task::Task<protocol::Block::Ptr> tag_invoke(ledger::tag_t<getBlockData> /*unused*/,
    LedgerInterface& ledger, protocol::BlockNumber blockNumber, int32_t blockFlag);

task::Task<TransactionCount> tag_invoke(
    ledger::tag_t<getTransactionCount> /*unused*/, LedgerInterface& ledger);

task::Task<protocol::BlockNumber> tag_invoke(
    ledger::tag_t<getCurrentBlockNumber> /*unused*/, LedgerInterface& ledger);

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

bcos::task::Task<bcos::crypto::HashType> tag_invoke(ledger::tag_t<getBlockHash> /*unused*/,
    LedgerInterface& ledger, protocol::BlockNumber blockNumber);

task::Task<std::optional<crypto::HashType>> tag_invoke(ledger::tag_t<getBlockHash> /*unused*/,
    auto& storage, protocol::BlockNumber blockNumber, FromStorage /*unused*/)
{
    LEDGER_LOG(TRACE) << "GetBlockHashByNumber request" << LOG_KV("blockNumber", blockNumber);
    if (blockNumber < 0)
    {
        BOOST_THROW_EXCEPTION(
            BCOS_ERROR(LedgerError::ErrorArgument, "GetBlockHashByNumber error, wrong argument"));
    }
    auto blockNumberString = boost::lexical_cast<std::string>(blockNumber);
    if (auto entry = co_await storage2::readOne(
            storage, transaction_executor::StateKeyView{SYS_NUMBER_2_HASH, blockNumberString}))
    {
        auto hashStr = entry->getField(0);
        bcos::crypto::HashType hash(hashStr, bcos::crypto::HashType::FromBinary);

        co_return std::make_optional(hash);
    }
    co_return std::nullopt;
}

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
    co_return -1;
}

task::Task<consensus::ConsensusNodeList> tag_invoke(
    ledger::tag_t<getNodeList> /*unused*/, auto& storage)
{
    LEDGER_LOG(DEBUG) << "GetNodeLis";
    auto nodeListEntry = co_await storage2::readOne(
        storage, transaction_executor::StateKeyView{SYS_CONSENSUS, "key"});
    if (!nodeListEntry)
    {
        co_return consensus::ConsensusNodeList{};
    }

    auto ledgerConsensusNodeList = decodeConsensusList(nodeListEntry->getField(0));
    consensus::ConsensusNodeList nodes =
        ledgerConsensusNodeList | RANGES::views::transform([](auto const& node) {
            auto nodeIDBin = fromHex(node.nodeID);
            crypto::NodeIDPtr nodeID = std::make_shared<crypto::KeyImpl>(nodeIDBin);
            auto type = magic_enum::enum_cast<consensus::Type>(node.type);
            if (!type)
            {
                BOOST_THROW_EXCEPTION(
                    InvalidConsensusType{} << bcos::Error::ErrorMessage(
                        fmt::format("GetNodeListByType failed, invalid node type: ", node.type)));
            }
            return consensus::ConsensusNode{nodeID, *type,
                node.voteWeight.template convert_to<uint64_t>(), 0,
                boost::lexical_cast<protocol::BlockNumber>(node.enableNumber)};
        }) |
        RANGES::to<std::vector>();

    auto entries =
        co_await storage2::readSome(storage, RANGES::views::transform(nodes, [](auto const& node) {
            std::string_view extraKey{node.nodeID->constData(), node.nodeID->size()};
            return transaction_executor::StateKeyView{SYS_CONSENSUS, extraKey};
        }));
    for (auto&& [node, entry] : RANGES::views::zip(nodes, entries))
    {
        if (entry)
        {
            bcostars::ConsensusNode tarsConsensusNode;
            concepts::serialize::decode(entry->get(), tarsConsensusNode);
            node.termWeight = tarsConsensusNode.termWeight;
        }
    }

    LEDGER_LOG(DEBUG) << "GetNodeListByType success" << LOG_KV("nodes size", nodes.size());
    co_return nodes;
}

task::Task<void> tag_invoke(ledger::tag_t<setNodeList> /*unused*/, auto& storage,
    RANGES::input_range auto&& nodeList, bool forceSet = false)
{
    LEDGER_LOG(DEBUG) << "SetNodeList request";
    auto ledgerNodeList = RANGES::views::transform(nodeList, [&](auto const& node) {
        ledger::ConsensusNode ledgerNode{.nodeID = node.nodeID->hex(),
            .voteWeight = node.voteWeight,
            .type = std::string(magic_enum::enum_name(node.type)),
            .enableNumber = boost::lexical_cast<std::string>(node.enableNumber)};
        return ledgerNode;
    }) | RANGES::to<std::vector>();
    auto nodeListEntry = encodeConsensusList(ledgerNodeList);
    co_await storage2::writeOne(storage, transaction_executor::StateKey{SYS_CONSENSUS, "key"},
        storage::Entry(std::move(nodeListEntry)));

    auto tarsNodeList = RANGES::views::filter(nodeList, [&](auto const& node) {
        return forceSet || node.termWeight > 0;
    }) | RANGES::views::transform([](auto const& node) {
        bcostars::ConsensusNode tarsConsensusNode;
        tarsConsensusNode.nodeID.assign(
            node.nodeID->constData(), node.nodeID->constData() + node.nodeID->size());
        tarsConsensusNode.type = static_cast<int>(node.type);
        tarsConsensusNode.voteWeight = node.voteWeight;
        tarsConsensusNode.termWeight = node.termWeight;
        tarsConsensusNode.enableNumber = node.enableNumber;
        return tarsConsensusNode;
    }) | RANGES::to<std::vector>();
    if (!tarsNodeList.empty())
    {
        co_await storage2::writeSome(
            storage, RANGES::views::transform(tarsNodeList, [](auto const& node) {
                bytes data;
                concepts::serialize::encode(node, data);
                return std::make_tuple(
                    transaction_executor::StateKey{
                        SYS_CONSENSUS, std::string_view(node.nodeID.data(), node.nodeID.size())},
                    storage::Entry(std::move(data)));
            }));
    }
    LEDGER_LOG(DEBUG) << "SetNodeList success" << LOG_KV("nodeList size", nodeList.size());
}

task::Task<std::optional<SystemConfigEntry>> tag_invoke(
    ledger::tag_t<getSystemConfig> /*unused*/, auto& storage, std::string_view key)
{
    if (auto entry = co_await storage2::readOne(
            storage, transaction_executor::StateKeyView(SYS_CONFIG, key)))
    {
        co_return entry->template getObject<SystemConfigEntry>();
    }
    co_return {};
}

}  // namespace bcos::ledger