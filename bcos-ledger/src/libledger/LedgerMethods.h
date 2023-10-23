#pragma once

#include "bcos-framework/consensus/ConsensusNode.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-table/src/LegacyStorageWrapper.h"
#include "bcos-task/AwaitableValue.h"
#include "bcos-tool/ConsensusNode.h"
#include "bcos-tool/VersionConverter.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Error.h"
#include <boost/container/small_vector.hpp>
#include <boost/throw_exception.hpp>
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

inline task::Task<void> tag_invoke(ledger::tag_t<prewriteBlock> /*unused*/, LedgerInterface& ledger,
    bcos::protocol::TransactionsPtr transactions, bcos::protocol::Block::ConstPtr block,
    bool withTransactionsAndReceipts, auto& storage)
{
    auto legacyStorage =
        std::make_shared<storage::LegacyStorageWrapper<std::decay_t<decltype(storage)>>>(storage);
    struct Awaitable
    {
        LedgerInterface& m_ledger;
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
                [this, handle](Error::Ptr error) {
                    if (error)
                    {
                        m_error = std::move(error);
                    }
                    handle.resume();
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
        .m_withTransactionsAndReceipts = withTransactionsAndReceipts,
        .m_storage = std::move(legacyStorage),
        .m_error = {}};
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

inline task::Task<protocol::BlockNumber> tag_invoke(
    ledger::tag_t<getCurrentBlockNumber> /*unused*/, LedgerInterface& ledger)
{
    struct Awaitable
    {
        LedgerInterface& m_ledger;
        std::variant<Error::Ptr, protocol::BlockNumber> m_result;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(CO_STD::coroutine_handle<> handle)
        {
            m_ledger.asyncGetBlockNumber(
                [this, handle](Error::Ptr error, bcos::protocol::BlockNumber blockNumber) {
                    if (error)
                    {
                        m_result.emplace<Error::Ptr>(std::move(error));
                    }
                    else
                    {
                        m_result.emplace<protocol::BlockNumber>(blockNumber);
                    }
                    handle.resume();
                });
        }
        protocol::BlockNumber await_resume()
        {
            if (std::holds_alternative<Error::Ptr>(m_result))
            {
                BOOST_THROW_EXCEPTION(*std::get<Error::Ptr>(m_result));
            }
            return std::get<protocol::BlockNumber>(m_result);
        }
    };

    co_return co_await Awaitable{.m_ledger = ledger, .m_result = {}};
}

inline task::Task<crypto::HashType> tag_invoke(ledger::tag_t<getBlockHash> /*unused*/,
    LedgerInterface& ledger, protocol::BlockNumber blockNumber)
{
    struct Awaitable
    {
        LedgerInterface& m_ledger;
        protocol::BlockNumber m_blockNumber;

        std::variant<Error::Ptr, crypto::HashType> m_result;

        constexpr static bool await_ready() noexcept { return false; }
        void await_suspend(CO_STD::coroutine_handle<> handle)
        {
            m_ledger.asyncGetBlockHashByNumber(
                m_blockNumber, [this, handle](Error::Ptr error, crypto::HashType hash) {
                    if (error)
                    {
                        m_result.emplace<Error::Ptr>(std::move(error));
                    }
                    else
                    {
                        m_result.emplace<crypto::HashType>(hash);
                    }
                    handle.resume();
                });
        }
        crypto::HashType await_resume()
        {
            if (std::holds_alternative<Error::Ptr>(m_result))
            {
                BOOST_THROW_EXCEPTION(*std::get<Error::Ptr>(m_result));
            }
            return std::get<crypto::HashType>(m_result);
        }
    };

    co_return co_await Awaitable{.m_ledger = ledger, .m_blockNumber = blockNumber, .m_result = {}};
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
    ledger::tag_t<getNodeList> /*unused*/, LedgerInterface& ledger, std::string_view type)
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
        LEDGER2_LOG(DEBUG) << "Get " << key << " failed, use default value"
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
        co_await getSystemConfigOrDefault(ledger, SYSTEM_KEY_TX_GAS_LIMIT, 0));
    ledgerConfig->setCompatibilityVersion(bcos::tool::toVersionNumber(
        std::get<0>(co_await getSystemConfig(ledger, SYSTEM_KEY_COMPATIBILITY_VERSION))));

    auto blockNumber = co_await getCurrentBlockNumber(ledger);
    auto hash = co_await getBlockHash(ledger, blockNumber);
    ledgerConfig->setHash(hash);

    ledger::Features features;
    ledgerConfig->setFeatures(features);

    auto enableRPBFT =
        (std::get<0>(co_await getSystemConfigOrDefault(ledger, SYSTEM_KEY_RPBFT_SWITCH, 0)) == 1);
    ledgerConfig->setConsensusType(
        std::string(enableRPBFT ? ledger::RPBFT_CONSENSUS_TYPE : ledger::PBFT_CONSENSUS_TYPE));
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