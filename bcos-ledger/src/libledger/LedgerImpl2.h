#pragma once

#include "Ledger.h"
#include "LedgerImpl.h"
#include "bcos-framework/consensus/ConsensusNode.h"
#include "bcos-framework/ledger/Ledger.h"
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-framework/transaction-executor/TransactionExecutor.h"
#include "bcos-table/src/LegacyStorageWrapper.h"
#include "bcos-task/AwaitableValue.h"
#include "bcos-tool/ConsensusNode.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/Error.h"
#include <boost/container/small_vector.hpp>
#include <boost/throw_exception.hpp>
#include <iterator>

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

inline task::Task<void> tag_invoke(ledger::tag_t<prewriteBlock> /*unused*/, Ledger& ledger,
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

}  // namespace bcos::ledger