#pragma once

#include "bcos-framework/consensus/ConsensusNode.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-table/src/LegacyStorageWrapper.h"
#include "bcos-task/AwaitableValue.h"
#include "bcos-tool/ConsensusNode.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Error.h"
#include <boost/throw_exception.hpp>
#include <exception>
#include <iterator>
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

task::Task<std::tuple<uint64_t, protocol::BlockNumber>> getSystemConfigOrDefault(
    LedgerInterface& ledger, std::string_view key, int64_t defaultValue);

task::Task<LedgerConfig::Ptr> tag_invoke(
    ledger::tag_t<getLedgerConfig> /*unused*/, LedgerInterface& ledger);

task::Task<Features> tag_invoke(ledger::tag_t<getFeatures> /*unused*/, LedgerInterface& ledger);
}  // namespace bcos::ledger