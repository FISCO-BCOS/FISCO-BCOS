#pragma once

#include "LedgerImpl.h"
#include "bcos-framework/consensus/ConsensusNode.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/storage2/StringPool.h"
#include "bcos-tool/ConsensusNode.h"
#include "bcos-utilities/Common.h"
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/transaction-executor/TransactionExecutor.h>
#include <boost/container/small_vector.hpp>
#include <boost/throw_exception.hpp>
#include <iterator>

namespace bcos::ledger
{

#define LEDGER2_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("LEDGER2")

template <bcos::transaction_executor::StateStorage Storage>
class LedgerImpl2
{
private:
    Storage& m_storage;
    protocol::BlockFactory& m_blockFactory;
    transaction_executor::TableNamePool& m_tableNamePool;

    decltype(m_blockFactory.transactionFactory()) m_transactionFactory;
    decltype(m_blockFactory.receiptFactory()) m_receiptFactory;
    decltype(m_blockFactory.cryptoSuite()->keyFactory()) m_keyFactory;

    template <std::same_as<bcos::concepts::ledger::HEADER>>
    task::Task<void> setBlockData(
        std::string_view blockNumberKey, protocol::IsBlock auto const& block)
    {
        LEDGER2_LOG(DEBUG) << "setBlockData header: " << blockNumberKey;
        auto blockHeader = block.blockHeaderConst();

        // number 2 header
        bcos::storage::Entry number2HeaderEntry;
        std::vector<bcos::byte> number2HeaderBuffer;
        bcos::concepts::serialize::encode(*blockHeader, number2HeaderBuffer);
        number2HeaderEntry.set(std::move(number2HeaderBuffer));
        co_await storage2::writeOne(m_storage,
            transaction_executor::StateKey{
                storage2::string_pool::makeStringID(m_tableNamePool, SYS_NUMBER_2_BLOCK_HEADER),
                blockNumberKey},
            std::move(number2HeaderEntry));

        // number 2 block hash
        bcos::storage::Entry hashEntry;
        hashEntry.set(concepts::bytebuffer::toView(blockHeader->hash()));
        co_await storage2::writeOne(m_storage,
            transaction_executor::StateKey{
                storage2::string_pool::makeStringID(m_tableNamePool, SYS_NUMBER_2_HASH),
                blockNumberKey},
            std::move(hashEntry));

        // block hash 2 number
        bcos::storage::Entry hash2NumberEntry;
        hash2NumberEntry.set(blockNumberKey);
        co_await storage2::writeOne(m_storage,
            transaction_executor::StateKey{
                storage2::string_pool::makeStringID(m_tableNamePool, SYS_HASH_2_NUMBER),
                concepts::bytebuffer::toView(block.blockHeaderConst()->hash())},
            std::move(hash2NumberEntry));

        // current number
        bcos::storage::Entry numberEntry;
        numberEntry.set(blockNumberKey);
        co_await storage2::writeOne(m_storage,
            transaction_executor::StateKey{
                storage2::string_pool::makeStringID(m_tableNamePool, SYS_CURRENT_STATE),
                SYS_KEY_CURRENT_NUMBER},
            std::move(numberEntry));

        co_return;
    }

    template <std::same_as<concepts::ledger::NONCES>>
    task::Task<void> setBlockData(
        std::string_view blockNumberKey, protocol::IsBlock auto const& block)
    {
        LEDGER2_LOG(DEBUG) << "setBlockData nonce " << blockNumberKey;

        auto blockNonceList = m_blockFactory.createBlock();
        blockNonceList->setNonceList(block.nonceList());
        bcos::storage::Entry number2NonceEntry;
        std::vector<bcos::byte> number2NonceBuffer;
        bcos::concepts::serialize::encode(*blockNonceList, number2NonceBuffer);
        number2NonceEntry.set(std::move(number2NonceBuffer));

        co_await storage2::writeOne(m_storage,
            transaction_executor::StateKey{
                storage2::string_pool::makeStringID(m_tableNamePool, SYS_BLOCK_NUMBER_2_NONCES),
                blockNumberKey},
            std::move(number2NonceEntry));

        co_return;
    }

    template <std::same_as<concepts::ledger::TRANSACTIONS_METADATA>>
    task::Task<void> setBlockData(
        std::string_view blockNumberKey, protocol::IsBlock auto const& block)
    {
        LEDGER2_LOG(DEBUG) << "setBlockData transaction metadata: " << blockNumberKey;

        auto transactionsBlock = m_blockFactory.createBlock();
        if (block.transactionsMetaDataSize() > 0)
        {
            for (size_t i = 0; i < block.transactionsMetaDataSize(); ++i)
            {
                auto originTransactionMetaData = block.transactionMetaData(i);
                auto transactionMetaData =
                    m_blockFactory.createTransactionMetaData(originTransactionMetaData->hash(),
                        std::string(originTransactionMetaData->to()));
                transactionsBlock->appendTransactionMetaData(std::move(transactionMetaData));
            }
        }
        else if (block.transactionsSize() > 0)
        {
            for (size_t i = 0; i < block.transactionsSize(); ++i)
            {
                auto transaction = block.transaction(i);
                auto transactionMetaData = m_blockFactory.createTransactionMetaData(
                    transaction->hash(), std::string(transaction->to()));
                transactionsBlock->appendTransactionMetaData(std::move(transactionMetaData));
            }
        }

        if (transactionsBlock->transactionsMetaDataSize() == 0)
        {
            LEDGER2_LOG(INFO)
                << "SetBlockData TRANSACTIONS_METADATA not found transaction meta data!";
            co_return;
        }

        std::vector<bcos::byte> number2TransactionHashesBuffer;
        bcos::concepts::serialize::encode(*transactionsBlock, number2TransactionHashesBuffer);

        bcos::storage::Entry number2TransactionHashesEntry;
        number2TransactionHashesEntry.set(std::move(number2TransactionHashesBuffer));
        co_await storage2::writeOne(m_storage,
            transaction_executor::StateKey{
                storage2::string_pool::makeStringID(m_tableNamePool, SYS_NUMBER_2_TXS),
                blockNumberKey},
            std::move(number2TransactionHashesEntry));

        co_return;
    }

    template <std::same_as<concepts::ledger::TRANSACTIONS>>
    task::Task<void> setBlockData(
        std::string_view blockNumberKey, protocol::IsBlock auto const& block)
    {
        LEDGER2_LOG(DEBUG) << "setBlockData transactions: " << blockNumberKey;

        if (block.transactionsMetaDataSize() == 0)
        {
            LEDGER2_LOG(INFO) << "SetBlockData TRANSACTIONS not found transaction meta data!";
            co_return;
        }

        if (block.transactionsMetaDataSize() != block.transactionsSize())
        {
            LEDGER2_LOG(INFO)
                << "SetBlockData TRANSACTIONS not equal transaction metas and transactions!";
            co_return;
        }

        auto availableTransactions =
            RANGES::iota_view<size_t, size_t>(0LU, block.transactionsSize()) |
            RANGES::views::transform([&block](uint64_t index) { return block.transaction(index); });

        auto hashes = availableTransactions | RANGES::views::transform([](auto& transaction) {
            return transaction->hash();
        });
        auto buffers = availableTransactions | RANGES::views::transform([](auto& transaction) {
            std::vector<bcos::byte> buffer;
            bcos::concepts::serialize::encode(*transaction, buffer);
            return buffer;
        });
        co_await setTransactions<true>(hashes, buffers);
    }

    template <std::same_as<concepts::ledger::RECEIPTS>>
    task::Task<void> setBlockData(
        std::string_view blockNumberKey, protocol::IsBlock auto const& block)
    {
        LEDGER2_LOG(DEBUG) << "setBlockData receipts: " << blockNumberKey;

        if (block.transactionsMetaDataSize() == 0)
        {
            LEDGER2_LOG(INFO) << "SetBlockData RECEIPTS not found transaction meta data!";
            co_return;
        }

        size_t totalTransactionCount = 0;
        size_t failedTransactionCount = 0;
        for (uint64_t i = 0; i < block.receiptsSize(); ++i)
        {
            auto receipt = block.receipt(i);
            if (receipt->status() != 0)
            {
                ++failedTransactionCount;
            }
            ++totalTransactionCount;
        }

        auto hashes = RANGES::iota_view<size_t, size_t>(0LU, block.transactionsMetaDataSize()) |
                      RANGES::views::transform([&block](uint64_t index) {
                          auto metaData = block.transactionMetaData(index);
                          return metaData->hash();
                      });
        auto buffers = RANGES::iota_view<size_t, size_t>(0LU, block.transactionsMetaDataSize()) |
                       RANGES::views::transform([&block](uint64_t index) {
                           auto receipt = block.receipt(index);
                           std::vector<bcos::byte> buffer;
                           bcos::concepts::serialize::encode(*receipt, buffer);
                           return buffer;
                       });

        co_await setTransactions<false>(hashes, buffers);

        LEDGER2_LOG(DEBUG) << LOG_DESC("Calculate tx counts in block")
                           << LOG_KV("number", blockNumberKey)
                           << LOG_KV("totalCount", totalTransactionCount)
                           << LOG_KV("failedCount", failedTransactionCount);

        auto transactionCount = co_await getStatus();
        transactionCount.total += totalTransactionCount;
        transactionCount.failed += failedTransactionCount;

        bcos::storage::Entry totalEntry;
        totalEntry.set(boost::lexical_cast<std::string>(transactionCount.total));
        co_await storage2::writeOne(m_storage,
            transaction_executor::StateKey{
                storage2::string_pool::makeStringID(m_tableNamePool, SYS_CURRENT_STATE),
                SYS_KEY_TOTAL_TRANSACTION_COUNT},
            std::move(totalEntry));

        if (transactionCount.failed > 0)
        {
            bcos::storage::Entry failedEntry;
            failedEntry.set(boost::lexical_cast<std::string>(transactionCount.failed));
            co_await storage2::writeOne(m_storage,
                transaction_executor::StateKey{
                    storage2::string_pool::makeStringID(m_tableNamePool, SYS_CURRENT_STATE),
                    SYS_KEY_TOTAL_FAILED_TRANSACTION},
                std::move(failedEntry));
        }

        LEDGER2_LOG(INFO) << LOG_DESC("setBlock")
                          << LOG_KV("number", block.blockHeaderConst()->number())
                          << LOG_KV("totalTxs", transactionCount.total)
                          << LOG_KV("failedTxs", transactionCount.failed)
                          << LOG_KV("incTxs", totalTransactionCount)
                          << LOG_KV("incFailedTxs", failedTransactionCount);
    }

    template <std::same_as<concepts::ledger::ALL>>
    task::Task<void> setBlockData(
        std::string_view blockNumberKey, bcos::concepts::block::Block auto& block)
    {
        LEDGER2_LOG(DEBUG) << "setBlockData all: " << blockNumberKey;

        co_await setBlockData<concepts::ledger::HEADER>(blockNumberKey, block);
        co_await setBlockData<concepts::ledger::TRANSACTIONS_METADATA>(blockNumberKey, block);
        co_await setBlockData<concepts::ledger::TRANSACTIONS>(blockNumberKey, block);
        co_await setBlockData<concepts::ledger::RECEIPTS>(blockNumberKey, block);
        co_await setBlockData<concepts::ledger::NONCES>(blockNumberKey, block);
    }

public:
    LedgerImpl2(Storage& storage, protocol::BlockFactory& blockFactory,
        transaction_executor::TableNamePool& tableNamePool)
      : m_storage(storage),
        m_blockFactory(blockFactory),
        m_tableNamePool(tableNamePool),
        m_transactionFactory(m_blockFactory.transactionFactory()),
        m_receiptFactory(m_blockFactory.receiptFactory()),
        m_keyFactory(m_blockFactory.cryptoSuite()->keyFactory())
    {}

    template <bcos::concepts::ledger::DataFlag... Flags>
    task::Task<void> setBlock(protocol::IsBlock auto const& block)
    {
        LEDGER2_LOG(INFO) << "setBlock: " << block.blockHeaderConst()->number();

        auto blockNumberStr = boost::lexical_cast<std::string>(block.blockHeaderConst()->number());
        (co_await setBlockData<Flags>(blockNumberStr, block), ...);
        co_return;
    }

    task::Task<ledger::LedgerConfig> getConfig()
    {
        auto sysConsensusID = storage2::string_pool::makeStringID(m_tableNamePool, SYS_CONSENSUS);

        // NodeList
        auto entry = co_await storage2::readOne(
            m_storage, std::tuple{sysConsensusID, std::string_view("key")});
        auto value = entry->get();
        boost::iostreams::stream<boost::iostreams::array_source> inputStream(
            value.data(), value.size());
        boost::archive::binary_iarchive archive(inputStream,
            boost::archive::no_header | boost::archive::no_codecvt | boost::archive::no_tracking);
        std::vector<ConsensusNode> consensusList;
        archive >> consensusList;

        ledger::LedgerConfig ledgerConfig;
        for (auto& node : consensusList)
        {
            constexpr static size_t MOSTLY_KEY_LENGTH = 32;
            boost::container::small_vector<bcos::byte, MOSTLY_KEY_LENGTH> nodeIDBin;
            boost::algorithm::unhex(
                node.nodeID.begin(), node.nodeID.end(), std::back_inserter(nodeIDBin));

            auto nodeID =
                m_keyFactory->createKey(bytesConstRef(nodeIDBin.data(), nodeIDBin.size()));
            auto consensusNode = std::make_shared<consensus::ConsensusNode>(
                nodeID, node.weight.convert_to<uint64_t>());
            if (node.type == CONSENSUS_SEALER)
            {
                ledgerConfig.mutableConsensusNodeList().emplace_back(std::move(consensusNode));
            }
            else
            {
                ledgerConfig.mutableObserverList()->emplace_back(std::move(consensusNode));
            }
        }

        constexpr static auto systemConfigKeys =
            std::to_array({SYSTEM_KEY_TX_COUNT_LIMIT, SYSTEM_KEY_CONSENSUS_LEADER_PERIOD,
                SYSTEM_KEY_TX_GAS_LIMIT, SYSTEM_KEY_COMPATIBILITY_VERSION});
        auto systemConfigTableID = storage2::string_pool::makeStringID(m_tableNamePool, SYS_CONFIG);
        auto it = co_await m_storage.read(
            systemConfigKeys |
            RANGES::views::transform(
                [&systemConfigTableID](
                    auto& key) -> std::tuple<transaction_executor::TableNameID, std::string_view> {
                    return std::tuple<transaction_executor::TableNameID, std::string_view>{
                        systemConfigTableID, key};
                }));
        int index = 0;
        while (co_await it.next())
        {
            if (!co_await it.hasValue())
            {
                BOOST_THROW_EXCEPTION(std::runtime_error("Missing system key!"));
            }
            auto&& entry = co_await it.value();
            auto [strValue, number] = entry.template getObject<SystemConfigEntry>();
            uint64_t value = 0;
            try
            {
                if (index != 3)
                {
                    value = boost::lexical_cast<uint64_t>(strValue);
                }
            }
            catch (std::exception& e)
            {
                LEDGER2_LOG(INFO) << "Empty value, key: " << index;
            }

            switch (index++)
            {
            case 0:
                ledgerConfig.setBlockTxCountLimit(value);
                break;
            case 1:
                ledgerConfig.setLeaderSwitchPeriod(value);
                break;
            case 2:
                ledgerConfig.setGasLimit({value, number});
                break;
            case 3:
                ledgerConfig.setCompatibilityVersion(bcos::tool::toVersionNumber(strValue));
                break;
            default:
                BOOST_THROW_EXCEPTION(UnexpectedRowIndex{});
                break;
            }
        }

        auto status = co_await getStatus();
        ledgerConfig.setBlockNumber(status.blockNumber);

        co_return ledgerConfig;
    }

    task::Task<bcos::concepts::ledger::Status> getStatus()
    {
        LEDGER2_LOG(TRACE) << "getStatus";
        constexpr static auto statusKeys = std::to_array({SYS_KEY_TOTAL_TRANSACTION_COUNT,
            SYS_KEY_TOTAL_FAILED_TRANSACTION, SYS_KEY_CURRENT_NUMBER});

        auto sysCurrentID = storage2::string_pool::makeStringID(m_tableNamePool, SYS_CURRENT_STATE);
        auto it = co_await m_storage.read(
            statusKeys | RANGES::views::transform([&sysCurrentID](std::string_view key) {
                return transaction_executor::StateKey{sysCurrentID, key};
            }));

        bcos::concepts::ledger::Status status;
        int i = 0;
        while (co_await it.next())
        {
            int64_t value = 0;
            if (co_await it.hasValue())
            {
                auto&& entry = co_await it.value();
                value = boost::lexical_cast<int64_t>(entry.get());
            }

            switch (i++)
            {
            case 0:
                status.total = value;
                break;
            case 1:
                status.failed = value;
                break;
            case 2:
                status.blockNumber = value;
                break;
            default:
                BOOST_THROW_EXCEPTION(UnexpectedRowIndex{});
                break;
            }
        }

        LEDGER2_LOG(TRACE) << "getStatus result: " << status.total << " | " << status.failed
                           << " | " << status.blockNumber;

        co_return status;
    }

    template <bool isTransaction>
    task::Task<std::vector<decltype(m_transactionFactory->createTransaction(
        std::declval<bytesConstRef>(), false, false))>>
    getTransactions(RANGES::input_range auto const& hashes)
    {
        auto tableNameID =
            isTransaction ?
                storage2::string_pool::makeStringID(m_tableNamePool, SYS_HASH_2_TX) :
                storage2::string_pool::makeStringID(m_tableNamePool, SYS_HASH_2_RECEIPT);

        LEDGER2_LOG(INFO) << "getTransactions: " << *tableNameID;

        auto it =
            co_await m_storage.read(hashes | RANGES::views::transform([&tableNameID](auto& hash) {
                return std::tuple{tableNameID,
                    std::string_view((const char*)RANGES::data(hash), RANGES::size(hash))};
            }));

        std::vector<decltype(m_transactionFactory->createTransaction(
            std::declval<bytesConstRef>(), false, false))>
            transactions;
        if constexpr (RANGES::sized_range<decltype(hashes)>)
        {
            transactions.reserve(RANGES::size(hashes));
        }

        while (co_await it.next())
        {
            if (co_await it.hasValue())
            {
                auto&& entry = co_await it.value();
                auto view = entry.get();
                transactions.emplace_back(m_transactionFactory->createTransaction(
                    bytesConstRef(view.data(), view.size()), false, false));
            }
            else
            {
                transactions.emplace_back({});
            }
        }

        co_return transactions;
    }

    template <bool isTransaction>
    task::Task<void> setTransactions(RANGES::range auto&& hashes, RANGES::range auto&& buffers)
    {
        if (RANGES::size(buffers) != RANGES::size(hashes))
        {
            BOOST_THROW_EXCEPTION(
                MismatchTransactionCount{} << bcos::error::ErrorMessage{"No match count!"});
        }
        auto tableNameID =
            isTransaction ?
                storage2::string_pool::makeStringID(m_tableNamePool, SYS_HASH_2_TX) :
                storage2::string_pool::makeStringID(m_tableNamePool, SYS_HASH_2_RECEIPT);

        LEDGER2_LOG(INFO) << "setTransactionBuffers: " << *tableNameID << " "
                          << RANGES::size(hashes);

        co_await m_storage.write(
            hashes | RANGES::views::transform([&tableNameID](auto const& hash) {
                return transaction_executor::StateKey{tableNameID,
                    std::string_view((const char*)RANGES::data(hash), RANGES::size(hash))};
            }),
            buffers | RANGES::views::transform([](auto&& buffer) {
                storage::Entry entry;
                entry.set(std::forward<decltype(buffer)>(buffer));

                return entry;
            }));

        co_return;
    }
};

}  // namespace bcos::ledger