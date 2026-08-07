/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file SingleNodeConsensus.cpp
 * @brief Built-in single-node consensus driver (定时出块).
 */

#include "SingleNodeConsensus.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-mempool/MemPoolImpl.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Common.h"
#include <boost/exception/diagnostic_information.hpp>
#include <chrono>
#include <future>

using namespace bcos;
using namespace bcos::single_consensus;

#define SINGLE_CONSENSUS_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("SINGLE_CONSENSUS")

SingleNodeConsensus::SingleNodeConsensus(
    std::shared_ptr<bcos::scheduler::SchedulerInterface> _scheduler,
    bcos::protocol::BlockFactory::Ptr _blockFactory, bcos::txpool::MemPoolImpl& _memPool,
    bcos::ledger::LedgerInterface::Ptr _ledger, std::uint32_t _compatibilityVersion,
    std::uint64_t _blockIntervalMs, bool _produceEmptyBlocks,
    bcos::crypto::HashType _prevRandao, std::string _feeRecipient, std::uint64_t _fixedTimestamp,
    std::uint64_t _gasLimit)
  : m_scheduler(std::move(_scheduler)),
    m_blockFactory(std::move(_blockFactory)),
    m_memPool(_memPool),
    m_ledger(std::move(_ledger)),
    m_compatibilityVersion(_compatibilityVersion),
    m_blockIntervalMs(_blockIntervalMs > 0 ? _blockIntervalMs : 1000),
    m_produceEmptyBlocks(_produceEmptyBlocks),
    m_prevRandao(_prevRandao),
    m_feeRecipient(std::move(_feeRecipient)),
    m_fixedTimestamp(_fixedTimestamp),
    m_gasLimit(_gasLimit)
{}

SingleNodeConsensus::~SingleNodeConsensus()
{
    stop();
}

void SingleNodeConsensus::start()
{
    if (m_running.exchange(true))
    {
        return;
    }
    SINGLE_CONSENSUS_LOG(INFO) << LOG_DESC("Single-node consensus started")
                               << LOG_KV("blockIntervalMs", m_blockIntervalMs)
                               << LOG_KV("produceEmptyBlocks", m_produceEmptyBlocks)
                               << LOG_KV("compatibilityVersion", m_compatibilityVersion);
    m_thread = std::thread([this] { loop(); });
}

void SingleNodeConsensus::stop()
{
    if (!m_running.exchange(false))
    {
        return;
    }
    if (m_thread.joinable())
    {
        m_thread.join();
    }
    SINGLE_CONSENSUS_LOG(INFO) << LOG_DESC("Single-node consensus stopped");
}

void SingleNodeConsensus::loop()
{
    while (m_running)
    {
        bool sealedTxBlock = false;
        try
        {
            sealedTxBlock = produceBlock();
        }
        catch (std::exception const& e)
        {
            SINGLE_CONSENSUS_LOG(ERROR) << LOG_DESC("produceBlock iteration threw")
                                        << LOG_KV("msg", boost::diagnostic_information(e));
        }
        catch (...)
        {
            SINGLE_CONSENSUS_LOG(ERROR) << LOG_DESC("produceBlock iteration threw (unknown)");
        }
        // Drain the mempool as fast as possible when transactions are available (a tx is
        // sealed immediately after submission instead of waiting for the next interval
        // tick — the EEST harness runs one tx per unit, so this removes ~1s/unit of
        // latency). Only pace the loop when nothing was sealed (empty block or skipped).
        if (!sealedTxBlock)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(m_blockIntervalMs));
        }
    }
}

bool SingleNodeConsensus::produceBlock()
{
    // Resolve the current head (block number + hash) from the ledger — commitBlock keeps
    // SYS_CURRENT_STATE / SYS_NUMBER_2_HASH current, so the next block always extends the
    // committed chain.
    std::promise<std::tuple<Error::Ptr, protocol::BlockNumber>> headPromise;
    m_ledger->asyncGetBlockNumber([&headPromise](
                                      Error::Ptr _error, protocol::BlockNumber _number) {
        headPromise.set_value(std::make_tuple(std::move(_error), _number));
    });
    auto [headError, headNumber] = headPromise.get_future().get();
    if (headError || headNumber < 0)
    {
        SINGLE_CONSENSUS_LOG(ERROR) << LOG_DESC("resolve head block number failed")
                                    << LOG_KV("msg", headError ? headError->errorMessage() : "");
        return false;
    }
    auto headHash = task::syncWait(ledger::getBlockHash(*m_ledger, headNumber));

    // Take pending transactions from the mempool.
    auto txs = m_memPool.takeAll();
    bool const sealedTxBlock = !txs.empty();

    // produceEmptyBlocks=false: only produce a block that carries at least one transaction
    // (used by EEST fixture runs so the produced block environment matches the fixture).
    if (txs.empty() && !m_produceEmptyBlocks)
    {
        SINGLE_CONSENSUS_LOG(DEBUG) << LOG_DESC("Skip empty block (produceEmptyBlocks=false)");
        return false;
    }

    // Assemble the block proposal.
    auto block = m_blockFactory->createBlock();
    auto header = m_blockFactory->blockHeaderFactory()->createBlockHeader();
    header->setNumber(headNumber + 1);
    header->setParentInfo(protocol::ParentInfo{headNumber, headHash});
    // Header timestamp is in milliseconds (executor divides by 1000 to get seconds, matching
    // EEST's currentTimestamp unit). fixed_timestamp (seconds) is pinned by the harness so the
    // produced block's timestamp matches the fixture; 0 = wall clock.
    header->setTimestamp(static_cast<int64_t>(
        m_fixedTimestamp > 0 ? m_fixedTimestamp * 1000 : static_cast<std::uint64_t>(utcTime())));
    header->setVersion(m_compatibilityVersion);
    header->setCoinbase(toAddress(m_feeRecipient));
    header->setPrevRandao(m_prevRandao);
    header->setGasLimit(u256(m_gasLimit));
    block->setBlockHeader(header);
    for (auto& tx : txs)
    {
        block->appendTransaction(tx);
    }
    SINGLE_CONSENSUS_LOG(INFO) << LOG_DESC("Producing block")
                               << LOG_KV("number", headNumber + 1)
                               << LOG_KV("txs", txs.size());

    // Execute the block through the scheduler (BaselineScheduler for v2). The scheduler
    // executes via the EVM, computes the state root and receipts into the block, and caches
    // the execution result for the commit below.
    std::promise<std::tuple<Error::Ptr, protocol::BlockHeader::Ptr, bool>> executePromise;
    m_scheduler->executeBlock(block, false,
        [&executePromise](
            Error::Ptr _error, protocol::BlockHeader::Ptr _header, bool _sysBlock) {
            executePromise.set_value(
                std::make_tuple(std::move(_error), std::move(_header), _sysBlock));
        });
    auto [executeError, executedHeader, sysBlock] = executePromise.get_future().get();
    (void)sysBlock;
    if (executeError || !executedHeader)
    {
        SINGLE_CONSENSUS_LOG(ERROR)
            << LOG_DESC("executeBlock failed")
            << LOG_KV("msg", executeError ? executeError->errorMessage() : "null header");
        return false;
    }

    // Commit the block through the scheduler + storage: BaselineScheduler persists via
    // prewriteBlockToBuffer + mergeBackStorage — the exact path PBFT/sealer use on v2 chains.
    std::promise<std::tuple<Error::Ptr, ledger::LedgerConfig::Ptr>> commitPromise;
    m_scheduler->commitBlock(executedHeader,
        [&commitPromise](Error::Ptr _error, ledger::LedgerConfig::Ptr _ledgerConfig) {
            commitPromise.set_value(std::make_tuple(std::move(_error), std::move(_ledgerConfig)));
        });
    auto [commitError, ledgerConfig] = commitPromise.get_future().get();
    (void)ledgerConfig;
    if (commitError)
    {
        SINGLE_CONSENSUS_LOG(ERROR) << LOG_DESC("commitBlock failed")
                                    << LOG_KV("msg", commitError->errorMessage());
        return false;
    }

    SINGLE_CONSENSUS_LOG(INFO) << LOG_DESC("Committed block")
                               << LOG_KV("number", executedHeader->number())
                               << LOG_KV("hash", executedHeader->hash().hexPrefixed())
                               << LOG_KV("txs", txs.size())
                               << LOG_KV("gasUsed", executedHeader->gasUsed().str())
                               << LOG_KV("stateRoot", executedHeader->stateRoot().hexPrefixed());
    return sealedTxBlock;
}
