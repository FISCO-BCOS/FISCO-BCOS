#pragma once

#include "Ledger.h"
#include "LedgerImpl.h"
#include "bcos-framework/consensus/ConsensusNode.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-task/AwaitableValue.h"
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

template <class Storage>
class LedgerImpl2
{
private:
    Storage& m_storage;
    protocol::BlockFactory& m_blockFactory;

    decltype(m_blockFactory.transactionFactory()) m_transactionFactory;
    decltype(m_blockFactory.receiptFactory()) m_receiptFactory;
    decltype(m_blockFactory.cryptoSuite()->keyFactory()) m_keyFactory;

    task::Task<ledger::LedgerConfig> getConfig()
    {
        auto sysConsensusID = SYS_CONSENSUS;

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
        auto it =
            co_await m_storage.read(systemConfigKeys | RANGES::views::transform([](auto& key) {
                return transaction_executor::StateKeyView{SYS_CONFIG, key};
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

        auto it =
            co_await m_storage.read(statusKeys | RANGES::views::transform([](std::string_view key) {
                return transaction_executor::StateKeyView{SYS_CURRENT_STATE, key};
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
        auto tableName = isTransaction ? SYS_HASH_2_TX : SYS_HASH_2_RECEIPT;
        LEDGER2_LOG(INFO) << "getTransactions: " << tableName;

        auto it =
            co_await m_storage.read(hashes | RANGES::views::transform([&tableName](auto& hash) {
                return transaction_executor::StateKeyView{tableName,
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
    task::Task<void> setTransactions(
        auto& storage, RANGES::range auto&& hashes, RANGES::range auto&& buffers)
    {
        if (RANGES::size(buffers) != RANGES::size(hashes))
        {
            BOOST_THROW_EXCEPTION(
                MismatchTransactionCount{} << bcos::error::ErrorMessage{"No match count!"});
        }
        auto tableNameID = isTransaction ? SYS_HASH_2_TX : SYS_HASH_2_RECEIPT;
        LEDGER2_LOG(INFO) << "setTransactionBuffers: " << tableNameID << " "
                          << RANGES::size(hashes);

        co_await storage.write(hashes | RANGES::views::transform([&tableNameID](auto const& hash) {
            return transaction_executor::StateKey{
                tableNameID, std::string_view((const char*)RANGES::data(hash), RANGES::size(hash))};
        }),
            buffers | RANGES::views::transform([](auto&& buffer) {
                storage::Entry entry;
                entry.set(std::forward<decltype(buffer)>(buffer));

                return entry;
            }));

        co_return;
    }
};

inline task::AwaitableValue<void> tag_invoke(ledger::tag_t<buildGenesisBlock> /*unused*/,
    Ledger& ledger, LedgerConfig::Ptr ledgerConfig, size_t gasLimit,
    const std::string_view& genesisData, std::string const& compatibilityVersion,
    bool isAuthCheck = false, std::string const& consensusType = "pbft",
    std::int64_t epochSealerNum = 4, std::int64_t epochBlockNum = 1000)
{
    ledger.buildGenesisBlock(std::move(ledgerConfig), gasLimit, genesisData, compatibilityVersion,
        isAuthCheck, consensusType, epochSealerNum, epochBlockNum);
    return {};
}

inline task::AwaitableValue<void> tag_invoke(ledger::tag_t<prewriteBlock> /*unused*/,
    Ledger& ledger, RANGES::range auto const& transactions, bcos::protocol::Block const& block,
    auto& storage)
{
    ledger.asyncPrewriteBlock();
}

}  // namespace bcos::ledger