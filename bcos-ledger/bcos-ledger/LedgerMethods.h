#pragma once

#include "ConsensusNode.h"
#include "bcos-concepts/Serialize.h"
#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-crypto/signature/key/KeyImpl.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/consensus/ConsensusNode.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/storage/StorageInterface.h"
#include "bcos-framework/storage2/Storage.h"
#include "bcos-framework/transaction-executor/StateKey.h"
#include "bcos-ledger/LedgerImpl.h"
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

task::Task<void> tag_invoke(ledger::tag_t<prewriteBlock> /*unused*/, LedgerInterface& ledger,
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

task::Task<protocol::Block::Ptr> tag_invoke(ledger::tag_t<getBlockData> /*unused*/,
    storage2::ReadableStorage<executor_v1::StateKeyView> auto& storage,
    protocol::BlockNumber _blockNumber, int32_t _blockFlag, protocol::BlockFactory& blockFactory)
{
    LEDGER_LOG(TRACE) << "GetBlockDataByNumber request" << LOG_KV("blockNumber", _blockNumber)
                      << LOG_KV("blockFlag", _blockFlag);
    if (_blockNumber < 0 || _blockFlag < 0)
    {
        LEDGER_LOG(INFO) << "GetBlockDataByNumber, wrong argument";
        BOOST_THROW_EXCEPTION(BCOS_ERROR(LedgerError::ErrorArgument, "Wrong argument"));
    }
    if (((_blockFlag & TRANSACTIONS) != 0) && ((_blockFlag & TRANSACTIONS_HASH) != 0))
    {
        LEDGER_LOG(INFO) << "GetBlockDataByNumber, wrong argument, transaction already has hash";
        BOOST_THROW_EXCEPTION(BCOS_ERROR(LedgerError::ErrorArgument, "Wrong argument"));
    }

    if ((_blockFlag & TRANSACTIONS) != 0 || (_blockFlag & RECEIPTS) != 0)
    {
        if (auto entry = co_await storage2::readOne(
                storage, executor_v1::StateKeyView{SYS_CURRENT_STATE, SYS_KEY_ARCHIVED_NUMBER}))
        {
            auto archivedBlockNumber = boost::lexical_cast<int64_t>(entry->get());
            if (_blockNumber < archivedBlockNumber)
            {
                LEDGER_LOG(INFO)
                    << "GetBlockDataByNumber, block number is larger than archived number";
                BOOST_THROW_EXCEPTION(BCOS_ERROR(LedgerError::ErrorArgument,
                    "Wrong argument, this block's transactions and receipts are archived"));
            }
        }
    }

    auto block = blockFactory.createBlock();
    auto blockNumberStr = std::to_string(_blockNumber);
    if (_blockFlag & HEADER)
    {
        if (auto entry = co_await storage2::readOne(
                storage, executor_v1::StateKeyView{SYS_NUMBER_2_BLOCK_HEADER, blockNumberStr}))
        {
            auto field = entry->getField(0);
            auto headerPtr = blockFactory.blockHeaderFactory()->createBlockHeader(
                bcos::bytesConstRef((bcos::byte*)field.data(), field.size()));
            block->setBlockHeader(std::move(headerPtr));
        }
        else
        {
            BOOST_THROW_EXCEPTION(NotFoundBlockHeader{});
        }
    }
    if (((_blockFlag & TRANSACTIONS) != 0) || ((_blockFlag & RECEIPTS) != 0) ||
        (_blockFlag & TRANSACTIONS_HASH) != 0)
    {
        if (auto txsEntry = co_await storage2::readOne(
                storage, executor_v1::StateKeyView{SYS_NUMBER_2_TXS, blockNumberStr}))
        {
            auto txs = txsEntry->getField(0);
            auto blockWithTxs =
                blockFactory.createBlock(bcos::bytesConstRef((bcos::byte*)txs.data(), txs.size()));
            auto hashes = blockWithTxs->transactionHashes() | ::ranges::to<std::vector>();
            LEDGER_LOG(TRACE) << "Get transactions hash list success, size:" << hashes.size();

            if ((_blockFlag & TRANSACTIONS) != 0)
            {
                auto transactions = co_await storage2::readSome(
                    storage, hashes | ::ranges::views::transform([](auto& hash) {
                        return executor_v1::StateKeyView{
                            SYS_HASH_2_TX, bcos::concepts::bytebuffer::toView(hash)};
                    }));
                for (auto& txEntry : transactions)
                {
                    auto field = txEntry->getField(0);
                    auto transaction = blockFactory.transactionFactory()->createTransaction(
                        bcos::bytesConstRef((bcos::byte*)field.data(), field.size()), false, false);
                    block->appendTransaction(std::move(transaction));
                }
            }

            if ((_blockFlag & RECEIPTS) != 0)
            {
                auto receipts = co_await storage2::readSome(
                    storage, ::ranges::views::transform(hashes, [](auto& hash) {
                        return executor_v1::StateKeyView{
                            SYS_HASH_2_RECEIPT, bcos::concepts::bytebuffer::toView(hash)};
                    }));
                for (auto& receiptEntry : receipts)
                {
                    auto field = receiptEntry->getField(0);
                    auto receipt = blockFactory.receiptFactory()->createReceipt(
                        bcos::bytesConstRef((bcos::byte*)field.data(), field.size()));
                    block->appendReceipt(std::move(receipt));
                }
            }

            if ((_blockFlag & TRANSACTIONS_HASH) != 0)
            {
                for (auto& hash : hashes)
                {
                    auto txMeta = blockFactory.createTransactionMetaData();
                    txMeta->setHash(hash);
                    block->appendTransactionMetaData(std::move(txMeta));
                }
            }
        }
    }
    co_return block;
}

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

task::Task<void> tag_invoke(ledger::tag_t<getLedgerConfig> /*unused*/, auto& storage,
    LedgerConfig& ledgerConfig, protocol::BlockNumber blockNumber,
    protocol::BlockFactory& blockFactory)
{
    auto nodeList = co_await ledger::getNodeList(storage);
    ledgerConfig.setConsensusNodeList(::ranges::views::filter(nodeList, [](auto& node) {
        return node.type == consensus::Type::consensus_sealer;
    }) | ::ranges::to<std::vector>());
    ledgerConfig.setObserverNodeList(::ranges::views::filter(nodeList, [](auto& node) {
        return node.type == consensus::Type::consensus_observer;
    }) | ::ranges::to<std::vector>());

    ledger::SystemConfigs sysConfig;
    co_await readFromStorage(sysConfig, storage, blockNumber);

    if (auto txLimitConfig = sysConfig.get(ledger::SystemConfig::tx_count_limit))
    {
        ledgerConfig.setBlockTxCountLimit(
            boost::lexical_cast<uint64_t>(txLimitConfig.value().first));
    }
    if (auto ledgerSwitchPeriodConfig =
            sysConfig.get(ledger::SystemConfig::consensus_leader_period))
    {
        ledgerConfig.setLeaderSwitchPeriod(
            boost::lexical_cast<uint64_t>(ledgerSwitchPeriodConfig.value().first));
    }
    auto txGasLimit = sysConfig.getOrDefault(ledger::SystemConfig::tx_gas_limit, "0");
    ledgerConfig.setGasLimit({boost::lexical_cast<uint64_t>(txGasLimit.first), txGasLimit.second});

    if (auto versionConfig = sysConfig.get(ledger::SystemConfig::compatibility_version))
    {
        ledgerConfig.setCompatibilityVersion(tool::toVersionNumber(versionConfig.value().first));
    }
    auto gasPrice = sysConfig.getOrDefault(ledger::SystemConfig::tx_gas_price, "0x0");
    ledgerConfig.setGasPrice(std::make_tuple(gasPrice.first, gasPrice.second));

    // Get block header to retrieve timestamp
    auto blockNumberStr = std::to_string(blockNumber);
    if (auto entry = co_await storage2::readOne(
            storage, executor_v1::StateKeyView{SYS_NUMBER_2_BLOCK_HEADER, blockNumberStr}))
    {
        auto field = entry->getField(0);
        auto headerPtr = blockFactory.blockHeaderFactory()->createBlockHeader(
            bcos::bytesConstRef((bcos::byte*)field.data(), field.size()));
        ledgerConfig.setTimestamp(headerPtr->timestamp());
        ledgerConfig.setHash(headerPtr->hash());
    }
    ledgerConfig.setBlockNumber(blockNumber);
    // auto blockHash = co_await getBlockHash(storage, blockNumber, fromStorage);
    // ledgerConfig.setHash(blockHash.value_or(crypto::HashType{}));

    Features features;
    co_await readFromStorage(features, storage, blockNumber);
    ledgerConfig.setFeatures(features);

    auto enableRPBFT =
        (sysConfig.getOrDefault(ledger::SystemConfig::feature_rpbft, "0").first == "1");
    ledgerConfig.setConsensusType(
        std::string(enableRPBFT ? ledger::RPBFT_CONSENSUS_TYPE : ledger::PBFT_CONSENSUS_TYPE));
    if (enableRPBFT)
    {
        ledgerConfig.setCandidateSealerNodeList(::ranges::views::filter(nodeList, [](auto& node) {
            return node.type == consensus::Type::consensus_candidate_sealer;
        }) | ::ranges::to<std::vector>());

        auto epochSealer =
            sysConfig.getOrDefault(ledger::SystemConfig::feature_rpbft_epoch_sealer_num,
                std::to_string(DEFAULT_EPOCH_SEALER_NUM));
        ledgerConfig.setEpochSealerNum(
            {boost::lexical_cast<uint64_t>(epochSealer.first), epochSealer.second});

        auto epochBlock =
            sysConfig.getOrDefault(ledger::SystemConfig::feature_rpbft_epoch_block_num,
                std::to_string(DEFAULT_EPOCH_BLOCK_NUM));
        ledgerConfig.setEpochBlockNum(
            {boost::lexical_cast<uint64_t>(epochBlock.first), epochBlock.second});

        auto notifyRotateFlagInfo =
            sysConfig.getOrDefault(ledger::SystemConfig::feature_rpbft_notify_rotate,
                std::to_string(DEFAULT_INTERNAL_NOTIFY_FLAG));
        ledgerConfig.setNotifyRotateFlagInfo(
            boost::lexical_cast<uint64_t>(notifyRotateFlagInfo.first));
    }
    auto auth = sysConfig.getOrDefault(ledger::SystemConfig::auth_check_status, "0");
    ledgerConfig.setAuthCheckStatus(boost::lexical_cast<uint32_t>(auth.first));
    auto [chainId, _] = sysConfig.getOrDefault(ledger::SystemConfig::web3_chain_id, "0");
    ledgerConfig.setChainId(bcos::toEvmC(boost::lexical_cast<u256>(chainId)));
    ledgerConfig.setBalanceTransfer(
        sysConfig.getOrDefault(ledger::SystemConfig::balance_transfer, "0").first != "0");

    if (auto executorVersion = sysConfig.get(ledger::SystemConfig::executor_version);
        executorVersion)
    {
        ledgerConfig.setExecutorVersion(boost::lexical_cast<int>(executorVersion.value().first));
    }
}

task::Task<Features> tag_invoke(ledger::tag_t<getFeatures> /*unused*/, LedgerInterface& ledger);

task::Task<protocol::TransactionReceipt::Ptr> tag_invoke(
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
    storage2::ReadableStorage<executor_v1::StateKeyView> auto& storage,
    protocol::BlockNumber blockNumber, FromStorage /*unused*/)
{
    LEDGER_LOG(TRACE) << "GetBlockHashByNumber request" << LOG_KV("blockNumber", blockNumber);
    if (blockNumber < 0)
    {
        BOOST_THROW_EXCEPTION(
            BCOS_ERROR(LedgerError::ErrorArgument, "GetBlockHashByNumber error, wrong argument"));
    }
    auto blockNumberString = boost::lexical_cast<std::string>(blockNumber);
    if (auto entry = co_await storage2::readOne(
            storage, executor_v1::StateKeyView{SYS_NUMBER_2_HASH, blockNumberString}))
    {
        auto hashStr = entry->getField(0);
        bcos::crypto::HashType hash(hashStr, bcos::crypto::HashType::FromBinary);

        co_return std::make_optional(hash);
    }
    co_return {};
}

task::Task<std::optional<protocol::BlockNumber>> tag_invoke(
    ledger::tag_t<getBlockNumber> /*unused*/,
    storage2::ReadableStorage<executor_v1::StateKey> auto& storage, crypto::HashType hash,
    FromStorage /*unused*/)
{
    LEDGER_LOG(TRACE) << "GetBlockNumberByHash request" << LOG_KV("blockHash", hash);
    if (auto entry = co_await storage2::readOne(storage,
            executor_v1::StateKeyView{SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(hash)}))
    {
        auto blockNumberStr = entry->getField(0);
        auto blockNumber = boost::lexical_cast<protocol::BlockNumber>(blockNumberStr);

        co_return std::make_optional(blockNumber);
    }
    co_return {};
}

task::Task<protocol::BlockNumber> tag_invoke(ledger::tag_t<getCurrentBlockNumber> /*unused*/,
    storage2::ReadableStorage<executor_v1::StateKey> auto& storage, FromStorage /*unused*/)
{
    if (auto blockNumberEntry = co_await storage2::readOne(
            storage, executor_v1::StateKeyView{SYS_CURRENT_STATE, SYS_KEY_CURRENT_NUMBER}))
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

task::Task<consensus::ConsensusNodeList> tag_invoke(ledger::tag_t<getNodeList> /*unused*/,
    storage2::ReadableStorage<executor_v1::StateKey> auto& storage)
{
    LEDGER_LOG(DEBUG) << "getNodeList";
    auto nodeListEntry =
        co_await storage2::readOne(storage, executor_v1::StateKeyView{SYS_CONSENSUS, "key"});
    if (!nodeListEntry)
    {
        co_return consensus::ConsensusNodeList{};
    }

    auto ledgerConsensusNodeList = decodeConsensusList(nodeListEntry->getField(0));
    consensus::ConsensusNodeList nodes =
        ledgerConsensusNodeList | ::ranges::views::transform([](auto const& node) {
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
        ::ranges::to<std::vector>();

    auto entries = co_await storage2::readSome(
        storage, ::ranges::views::transform(nodes, [](auto const& node) {
            std::string_view extraKey{node.nodeID->constData(), node.nodeID->size()};
            return executor_v1::StateKeyView{SYS_CONSENSUS, extraKey};
        }));
    for (auto&& [node, entry] : ::ranges::views::zip(nodes, entries))
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

task::Task<void> tag_invoke(ledger::tag_t<setNodeList> /*unused*/,
    storage2::ReadableStorage<executor_v1::StateKey> auto& storage,
    ::ranges::input_range auto&& nodeList, bool forceSet = false)
{
    LEDGER_LOG(DEBUG) << "SetNodeList request";
    auto ledgerNodeList = ::ranges::views::transform(nodeList, [&](auto const& node) {
        ledger::ConsensusNode ledgerNode{.nodeID = node.nodeID->hex(),
            .voteWeight = node.voteWeight,
            .type = std::string(magic_enum::enum_name(node.type)),
            .enableNumber = boost::lexical_cast<std::string>(node.enableNumber)};
        return ledgerNode;
    }) | ::ranges::to<std::vector>();
    auto nodeListEntry = encodeConsensusList(ledgerNodeList);
    co_await storage2::writeOne(storage, executor_v1::StateKey{SYS_CONSENSUS, "key"},
        storage::Entry(std::move(nodeListEntry)));

    auto tarsNodeList = ::ranges::views::filter(nodeList, [&](auto const& node) {
        return forceSet || node.termWeight > 0;
    }) | ::ranges::views::transform([](auto const& node) {
        bcostars::ConsensusNode tarsConsensusNode;
        tarsConsensusNode.nodeID.assign(
            node.nodeID->constData(), node.nodeID->constData() + node.nodeID->size());
        tarsConsensusNode.type = static_cast<int>(node.type);
        tarsConsensusNode.voteWeight = node.voteWeight;
        tarsConsensusNode.termWeight = node.termWeight;
        tarsConsensusNode.enableNumber = node.enableNumber;
        return tarsConsensusNode;
    }) | ::ranges::to<std::vector>();
    if (!tarsNodeList.empty())
    {
        co_await storage2::writeSome(
            storage, ::ranges::views::transform(tarsNodeList, [](auto const& node) {
                bytes data;
                concepts::serialize::encode(node, data);
                return std::make_tuple(
                    executor_v1::StateKey{
                        SYS_CONSENSUS, std::string_view(node.nodeID.data(), node.nodeID.size())},
                    storage::Entry(std::move(data)));
            }));
    }
    LEDGER_LOG(DEBUG) << "SetNodeList success" << LOG_KV("nodeList size", nodeList.size());
}

task::Task<std::optional<SystemConfigEntry>> tag_invoke(ledger::tag_t<getSystemConfig> /*unused*/,
    storage2::ReadableStorage<executor_v1::StateKey> auto& storage, std::string_view key)
{
    if (auto entry =
            co_await storage2::readOne(storage, executor_v1::StateKeyView(SYS_CONFIG, key)))
    {
        co_return entry->template getObject<SystemConfigEntry>();
    }
    co_return {};
}

}  // namespace bcos::ledger