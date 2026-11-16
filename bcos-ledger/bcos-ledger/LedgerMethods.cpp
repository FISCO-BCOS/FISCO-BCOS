#include "LedgerMethods.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/ledger/SystemConfigs.h"
#include "bcos-tool/VersionConverter.h"
#include <bcos-executor/src/Common.h>
#include <boost/exception/diagnostic_information.hpp>
#include <exception>
#include <functional>

bcos::task::Task<void> bcos::ledger::prewriteBlockToStorage(LedgerInterface& ledger,
    bcos::protocol::ConstTransactionsPtr transactions, bcos::protocol::Block::ConstPtr block,
    bool withTransactionsAndReceipts, storage::StorageInterface::Ptr storage,
    std::optional<bcos::crypto::HashType> blockHashOverride, bool writeNonces)
{
    co_await ledger.prewriteBlock(std::move(storage), std::move(transactions), std::move(block),
        withTransactionsAndReceipts, std::move(blockHashOverride), writeNonces);
}

bcos::ledger::PrewriteBlockRows bcos::ledger::buildPrewriteBlockRows(
    protocol::Block::ConstPtr block, protocol::ConstTransactionsPtr blockTxs,
    protocol::BlockFactory& blockFactory, bcos::crypto::HashType const& blockHash, bool writeNonces)
{
    auto header = block->blockHeader();
    auto blockNumberStr = boost::lexical_cast<std::string>(header->number());

    PrewriteBlockRows out;
    out.rows.reserve(6);

    // number 2 hash
    storage::Entry hashEntry;
    hashEntry.set(blockHash.asBytes());
    out.rows.emplace_back(
        executor_v1::StateKey{SYS_NUMBER_2_HASH, blockNumberStr}, std::move(hashEntry));

    // hash 2 number
    storage::Entry hash2NumberEntry;
    hash2NumberEntry.set(blockNumberStr);
    out.rows.emplace_back(
        executor_v1::StateKey{SYS_HASH_2_NUMBER, bcos::concepts::bytebuffer::toView(blockHash)},
        std::move(hash2NumberEntry));

    // number 2 header
    bytes headerBuffer;
    header->encode(headerBuffer);
    storage::Entry number2HeaderEntry;
    number2HeaderEntry.set(std::move(headerBuffer));
    out.rows.emplace_back(executor_v1::StateKey{SYS_NUMBER_2_BLOCK_HEADER, blockNumberStr},
        std::move(number2HeaderEntry));

    // number 2 nonce — nonces prefer the external tx list over the block's inline txs
    if (writeNonces)
    {
        auto nonceBlock = blockFactory.createBlock();
        protocol::NonceList nonceList;
        if (blockTxs)
        {
            for (auto const& tx : *blockTxs)
            {
                nonceList.emplace_back(tx->nonce());
            }
        }
        else
        {
            for (auto tx : block->transactions())
            {
                nonceList.emplace_back(tx->nonce());
            }
        }

        nonceBlock->setNonceList(nonceList);
        bytes nonceBuffer;
        nonceBlock->encode(nonceBuffer);
        storage::Entry number2NonceEntry;
        number2NonceEntry.set(std::move(nonceBuffer));
        out.rows.emplace_back(executor_v1::StateKey{SYS_BLOCK_NUMBER_2_NONCES, blockNumberStr},
            std::move(number2NonceEntry));
    }

    // current number
    storage::Entry numberEntry;
    numberEntry.set(blockNumberStr);
    out.rows.emplace_back(
        executor_v1::StateKey{SYS_CURRENT_STATE, SYS_KEY_CURRENT_NUMBER}, std::move(numberEntry));

    // number 2 transactions (metadata only; rebuilt from full transactions when the block
    // carries no metadata)
    out.transactionsBlock = blockFactory.createBlock();
    if (block->transactionsMetaDataSize() > 0)
    {
        for (auto originTransactionMetaData : block->transactionMetaDatas())
        {
            auto transactionMetaData = blockFactory.createTransactionMetaData(
                originTransactionMetaData->hash(), std::string(originTransactionMetaData->to()));
            out.transactionsBlock->appendTransactionMetaData(std::move(transactionMetaData));
        }
    }
    else if (block->transactionsSize() > 0)
    {
        for (auto transaction : block->transactions())
        {
            auto transactionMetaData = blockFactory.createTransactionMetaData(
                transaction->hash(), std::string(transaction->to()));
            out.transactionsBlock->appendTransactionMetaData(std::move(transactionMetaData));
        }
    }
    else if (header->number() > 0)
    {
        LEDGER_LOG(WARNING) << "Empty transactions and metadata, empty block?"
                            << LOG_KV("blockNumber", blockNumberStr);
    }
    bytes transactionsBuffer;
    out.transactionsBlock->encode(transactionsBuffer);
    storage::Entry number2TransactionHashesEntry;
    number2TransactionHashesEntry.set(std::move(transactionsBuffer));
    out.rows.emplace_back(executor_v1::StateKey{SYS_NUMBER_2_TXS, blockNumberStr},
        std::move(number2TransactionHashesEntry));

    return out;
}

bcos::task::Task<void> bcos::ledger::tag_invoke(
    ledger::tag_t<storeTransactionsAndReceipts> /*unused*/, LedgerInterface& ledger,
    bcos::protocol::ConstTransactionsPtr blockTxs, bcos::protocol::Block::ConstPtr block)
{
    auto error = ledger.storeTransactionsAndReceipts(std::move(blockTxs), std::move(block));
    if (error)
    {
        BOOST_THROW_EXCEPTION(*error);
    }
    co_return;
}

bcos::task::Task<bcos::protocol::Block::Ptr> bcos::ledger::tag_invoke(
    ledger::tag_t<getBlockData> /*unused*/, LedgerInterface& ledger,
    protocol::BlockNumber blockNumber, int32_t blockFlag)
{
    co_return co_await ledger.getBlockDataByNumber(blockNumber, blockFlag);
}

bcos::task::Task<bcos::ledger::TransactionCount> bcos::ledger::tag_invoke(
    ledger::tag_t<getTransactionCount> /*unused*/, LedgerInterface& ledger)
{
    auto [total, failed, blockNumber] = co_await ledger.getTotalTransactionCount();
    co_return TransactionCount{
        .total = total,
        .failed = failed,
        .blockNumber = blockNumber,
    };
}

bcos::task::Task<bcos::protocol::BlockNumber> bcos::ledger::tag_invoke(
    ledger::tag_t<getCurrentBlockNumber> /*unused*/, LedgerInterface& ledger)
{
    co_return co_await ledger.getBlockNumber();
}
bcos::task::Task<bcos::crypto::HashType> bcos::ledger::tag_invoke(
    ledger::tag_t<getBlockHash> /*unused*/, LedgerInterface& ledger,
    protocol::BlockNumber blockNumber)
{
    co_return co_await ledger.getBlockHashByNumber(blockNumber);
}

bcos::task::Task<bcos::protocol::BlockNumber> bcos::ledger::tag_invoke(
    bcos::ledger::tag_t<bcos::ledger::getBlockNumber> /*unused*/,
    bcos::ledger::LedgerInterface& ledger, bcos::crypto::HashType hash)
{
    co_return co_await ledger.getBlockNumberByHash(std::move(hash));
}

bcos::task::Task<std::optional<bcos::ledger::SystemConfigEntry>> bcos::ledger::tag_invoke(
    ledger::tag_t<getSystemConfig> /*unused*/, LedgerInterface& ledger, std::string_view key)
{
    try
    {
        auto config = co_await ledger.getSystemConfigByKey(key);
        co_return std::optional<SystemConfigEntry>{std::move(config)};
    }
    catch (bcos::Error const& error)
    {
        if (error.errorCode() == LedgerError::EmptyEntry ||
            error.errorCode() == LedgerError::ErrorArgument ||
            error.errorCode() == LedgerError::GetStorageError)
        {
            co_return std::nullopt;
        }
        throw;
    }
}

bcos::task::Task<bcos::consensus::ConsensusNodeList> bcos::ledger::tag_invoke(
    ledger::tag_t<getNodeList> /*unused*/, LedgerInterface& ledger, std::string_view type)
{
    co_return co_await ledger.getNodeListByType(type);
}

bcos::task::Task<void> bcos::ledger::tag_invoke(
    ledger::tag_t<getLedgerConfig> /*unused*/, LedgerInterface& ledger, LedgerConfig& ledgerConfig)
{
    // Fetch-then-assemble shape mirrors the storage2-view getLedgerConfig overload in
    // LedgerMethods.h: resolve all inputs (at THIS path's block basis — sysConfigs /
    // features effective at blockNumber + 1) and delegate the shared, consensus-relevant
    // assembly to applyLedgerConfig so the two paths cannot drift from each other.
    auto nodeList = co_await getNodeList(ledger, {});

    auto blockNumber = co_await getCurrentBlockNumber(ledger);
    auto sysConfig = co_await ledger.fetchAllSystemConfigs(blockNumber + 1);

    // Timestamp from the block header; hash from the SYS_NUMBER_2_HASH row (works for OP
    // headers whose in-memory BlockHeader::hash() would throw).
    std::optional<int64_t> timestamp;
    std::optional<u256> headBaseFee;
    auto block = co_await getBlockData(ledger, blockNumber, HEADER);
    if (block && block->blockHeader())
    {
        timestamp = block->blockHeader()->timestamp();
        headBaseFee = protocol::blockBaseFee(*block->blockHeader());
    }
    auto blockHash = co_await getBlockHash(ledger, blockNumber);
    Features features;
    if (executorVersionOf(sysConfig) < ledger::ETHEREUM_EXECUTOR_VERSION)
    {
        features = co_await getFeatures(ledger);
    }

    applyLedgerConfig(ledgerConfig, nodeList, sysConfig, features, blockNumber, timestamp,
        std::optional<crypto::HashType>{blockHash}, headBaseFee);
    co_return;
}

bcos::task::Task<bcos::ledger::Features> bcos::ledger::tag_invoke(
    ledger::tag_t<getFeatures> /*unused*/, LedgerInterface& ledger)
{
    auto blockNumber = co_await getCurrentBlockNumber(ledger);
    Features features;
    try
    {
        features = co_await ledger.fetchAllFeatures(blockNumber + 1);
    }
    catch (...)
    {
        LEDGER2_LOG(DEBUG) << LOG_DESC("fetch features failed")
                           << LOG_KV("msg", boost::current_exception_diagnostic_information());
    }

    co_return features;
}

bcos::task::Task<bool> bcos::ledger::tag_invoke(ledger::tag_t<getFeature> /*unused*/,
    LedgerInterface& ledger, ledger::Features::Flag flag, protocol::BlockNumber blockNumber)
{
    // Single-flag read: Ledger overrides fetchFeature with one SYS_CONFIG row instead of
    // fetchAllFeatures' scan of every feature key (~60 rows). Used by the historical
    // state-read path (feature_mpt_state_root) which needs exactly one flag; degrades
    // to false (scenario A) on any failure, the same honest default as getFeatures'
    // empty-set fallback.
    try
    {
        co_return co_await ledger.fetchFeature(flag, blockNumber);
    }
    catch (...)
    {
        LEDGER2_LOG(DEBUG) << LOG_DESC("fetch feature failed")
                           << LOG_KV("flag", magic_enum::enum_name(flag))
                           << LOG_KV("msg", boost::current_exception_diagnostic_information());
        co_return false;
    }
}

bcos::task::Task<bcos::protocol::TransactionReceipt::Ptr> bcos::ledger::tag_invoke(
    ledger::tag_t<getReceipt> /*unused*/, LedgerInterface& ledger, crypto::HashType const& txHash)
{
    auto result = co_await ledger.getTransactionReceiptByHash(txHash, false);
    if (result.error)
    {
        BOOST_THROW_EXCEPTION(*result.error);
    }
    co_return std::move(result.receipt);
}

bcos::task::Task<bcos::protocol::TransactionsConstPtr> bcos::ledger::tag_invoke(
    ledger::tag_t<getTransactions> /*unused*/, LedgerInterface& ledger, crypto::HashListPtr hashes)
{
    auto result = co_await ledger.getBatchTxsByHashList(std::move(hashes), false);
    co_return std::move(result.first);
}

bcos::task::Task<std::optional<bcos::storage::Entry>> bcos::ledger::tag_invoke(
    ledger::tag_t<getStorageAt> /*unused*/, LedgerInterface& ledger, std::string_view address,
    std::string_view key, bcos::protocol::BlockNumber number)
{
    co_return co_await ledger.getStorageAt(address, key, number);
}

bcos::task::Task<
    std::shared_ptr<std::map<bcos::protocol::BlockNumber, bcos::protocol::NonceListPtr>>>
bcos::ledger::tag_invoke(ledger::tag_t<getNonceList> /*unused*/, LedgerInterface& ledger,
    bcos::protocol::BlockNumber startNumber, int64_t offset)
{
    co_return co_await ledger.getNonceList(startNumber, offset);
}
