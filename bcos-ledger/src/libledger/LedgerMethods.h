#pragma once

#include "Ledger.h"
#include "bcos-framework/consensus/ConsensusNode.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-table/src/LegacyStorageWrapper.h"
#include "bcos-task/AwaitableValue.h"
#include "bcos-tool/ConsensusNode.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Error.h"
#include <boost/container/small_vector.hpp>
#include <boost/throw_exception.hpp>
#include <exception>
#include <iterator>
#include <type_traits>
#include <variant>

namespace bcos::ledger
{

#define LEDGER2_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("LEDGER2")

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

inline task::Task<void> tag_invoke(ledger::tag_t<prewriteBlock> /*unused*/, LedgerInterface& ledger,
    bcos::protocol::TransactionsPtr transactions, bcos::protocol::Block::ConstPtr block,
    bool withTransactionsAndReceipts, auto& storage)
{
    auto legacyStorage =
        std::make_shared<storage::LegacyStorageWrapper<std::decay_t<decltype(storage)>>>(storage);
    struct Awaitable
    {
        Ledger& m_ledger;
        decltype(transactions) m_transactions;
        decltype(block) m_block;
        bool m_withTransactionsAndReceipts{};
        decltype(legacyStorage) m_storage;

        Error::Ptr m_error;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(CO_STD::coroutine_handle<> handle)
        {
            m_ledger.asyncPrewriteBlock(
                m_storage, std::move(m_transactions), std::move(m_block),
                [this](Error::Ptr error) {
                    if (error)
                    {
                        m_error = std::move(error);
                    }
                },
                m_withTransactionsAndReceipts);
        }
        void await_resume()
        {
            if (m_error)
            {
                BOOST_THROW_EXCEPTION(*m_error);
            }
        }
    };

    co_await Awaitable{.m_ledger = ledger,
        .m_transactions = std::move(transactions),
        .m_block = std::move(block),
        .m_storage = std::move(legacyStorage),
        .m_withTransactionsAndReceipts = withTransactionsAndReceipts};
}

inline task::Task<TransactionCount> tag_invoke(
    ledger::tag_t<getTransactionCount> /*unused*/, LedgerInterface& ledger)
{
    struct Awaitable
    {
        LedgerInterface& m_ledger;
        std::variant<Error::Ptr, TransactionCount> m_result;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(CO_STD::coroutine_handle<> handle)
        {
            m_ledger.asyncGetTotalTransactionCount(
                [this, handle](Error::Ptr error, int64_t total, int64_t failed,
                    bcos::protocol::BlockNumber blockNumber) {
                    if (error)
                    {
                        m_result.emplace<Error::Ptr>(std::move(error));
                    }
                    else
                    {
                        m_result.emplace<TransactionCount>(TransactionCount{
                            .total = total,
                            .failed = failed,
                            .blockNumber = blockNumber,
                        });
                    }
                    handle.resume();
                });
        }
        TransactionCount await_resume()
        {
            if (std::holds_alternative<Error::Ptr>(m_result))
            {
                BOOST_THROW_EXCEPTION(*std::get<Error::Ptr>(m_result));
            }
            return std::get<TransactionCount>(m_result);
        }
    };

    co_return co_await Awaitable{.m_ledger = ledger, .m_result = {}};
}

inline task::Task<SystemConfigEntry> tag_invoke(
    ledger::tag_t<getSystemConfig> /*unused*/, LedgerInterface& ledger, std::string_view key)
{
    struct Awaitable
    {
        LedgerInterface& m_ledger;
        std::string_view m_key;
        std::variant<Error::Ptr, SystemConfigEntry> m_result;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(CO_STD::coroutine_handle<> handle)
        {
            m_ledger.asyncGetSystemConfigByKey(
                m_key, [this, handle](Error::Ptr error, std::string value,
                           bcos::protocol::BlockNumber blockNumber) {
                    if (error)
                    {
                        m_result.emplace<Error::Ptr>(std::move(error));
                    }
                    else
                    {
                        m_result.emplace<SystemConfigEntry>(std::move(value), blockNumber);
                    }
                    handle.resume();
                });
        }
        SystemConfigEntry await_resume()
        {
            if (std::holds_alternative<Error::Ptr>(m_result))
            {
                BOOST_THROW_EXCEPTION(*std::get<Error::Ptr>(m_result));
            }
            return std::move(std::get<SystemConfigEntry>(m_result));
        }
    };

    co_return co_await Awaitable{.m_ledger = ledger, .m_key = key, .m_result = {}};
}

inline task::Task<consensus::ConsensusNodeList> tag_invoke(
    ledger::tag_t<getNodeList>, LedgerInterface& ledger, std::string_view type)
{
    struct Awaitable
    {
        LedgerInterface& m_ledger;
        std::string_view m_type;
        std::variant<Error::Ptr, consensus::ConsensusNodeList> m_result;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(CO_STD::coroutine_handle<> handle)
        {
            m_ledger.asyncGetNodeListByType(
                m_type, [this, handle](
                            Error::Ptr error, consensus::ConsensusNodeListPtr consensusNodeList) {
                    if (error)
                    {
                        m_result.emplace<Error::Ptr>(std::move(error));
                    }
                    else
                    {
                        m_result.emplace<consensus::ConsensusNodeList>(
                            std::move(*consensusNodeList));
                    }
                    handle.resume();
                });
        }
        consensus::ConsensusNodeList await_resume()
        {
            if (std::holds_alternative<Error::Ptr>(m_result))
            {
                BOOST_THROW_EXCEPTION(*std::get<Error::Ptr>(m_result));
            }
            return std::move(std::get<consensus::ConsensusNodeList>(m_result));
        }
    };

    co_return co_await Awaitable{.m_ledger = ledger, .m_type = type, .m_result = {}};
}

inline task::Task<std::tuple<uint64_t, protocol::BlockNumber>> getSystemConfigOrDefault(
    auto& ledger, std::string_view key, int64_t defaultValue)
{
    try
    {
        auto [value, blockNumber] =
            co_await getSystemConfig(ledger, SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM);

        co_return std::tuple<uint64_t, protocol::BlockNumber>{
            boost::lexical_cast<uint64_t>(value), blockNumber};
    }
    catch (std::exception& e)
    {
        LEDGER_LOG(DEBUG) << "Get " << key << " failed, use default value"
                          << LOG_KV("defaultValue", defaultValue);
        co_return std::tuple<uint64_t, protocol::BlockNumber>{defaultValue, 0};
    }
}

inline task::Task<LedgerConfig::Ptr> tag_invoke(
    ledger::tag_t<getLedgerConfig> /*unused*/, LedgerInterface& ledger)
{
    auto ledgerConfig = std::make_shared<ledger::LedgerConfig>();
    ledgerConfig->setConsensusNodeList(co_await getNodeList(ledger, ledger::CONSENSUS_SEALER));
    ledgerConfig->setObserverNodeList(co_await getNodeList(ledger, ledger::CONSENSUS_OBSERVER));

    ledgerConfig->setBlockTxCountLimit(boost::lexical_cast<uint64_t>(
        std::get<0>(co_await getSystemConfig(ledger, SYSTEM_KEY_TX_COUNT_LIMIT))));
    ledgerConfig->setLeaderSwitchPeriod(boost::lexical_cast<uint64_t>(
        std::get<0>(co_await getSystemConfig(ledger, SYSTEM_KEY_CONSENSUS_LEADER_PERIOD))));
    ledgerConfig->setGasLimit(
        co_await getSystemConfigOrDefault(ledger, SYSTEM_KEY_CONSENSUS_LEADER_PERIOD, 0));

    auto transactionCount = co_await getTransactionCount(ledger);
    ledger::Features features;
    co_await features.readFromLedger(ledger, transactionCount.blockNumber);

    auto enableRPBFT =
        (std::get<0>(co_await getSystemConfig(ledger, SYSTEM_KEY_RPBFT_SWITCH)) == "1");
    if (enableRPBFT)
    {
        ledgerConfig->setCandidateSealerNodeList(
            co_await getNodeList(ledger, ledger::CONSENSUS_CANDIDATE_SEALER));
        ledgerConfig->setEpochSealerNum(co_await getSystemConfigOrDefault(
            ledger, SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM, DEFAULT_EPOCH_SEALER_NUM));
        ledgerConfig->setEpochSealerNum(co_await getSystemConfigOrDefault(
            ledger, SYSTEM_KEY_RPBFT_EPOCH_BLOCK_NUM, DEFAULT_EPOCH_BLOCK_NUM));
        ledgerConfig->setNotifyRotateFlagInfo(std::get<0>(co_await getSystemConfigOrDefault(
            ledger, INTERNAL_SYSTEM_KEY_NOTIFY_ROTATE, DEFAULT_INTERNAL_NOTIFY_FLAG)));
    }

    co_return ledgerConfig;
}
}  // namespace bcos::ledger