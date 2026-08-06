/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file SingleNodeConsensus.h
 * @brief Built-in single-node consensus driver (定时出块).
 */

#pragma once

#include "bcos-framework/dispatcher/SchedulerInterface.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/protocol/BlockFactory.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/FixedBytes.h"
#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace bcos::txpool
{
class MemPoolImpl;
}  // namespace bcos::txpool

namespace bcos::single_consensus
{
/// Built-in single-node consensus driver.
///
/// Produces a block every @p blockIntervalMs by assembling a block proposal from the
/// in-process mempool (sendRawTransaction routes there in this mode, bypassing the legacy
/// txpool/sealer/pbft — see EthEndpoint / Initializer) and driving it through the standard
/// scheduler commit path:
///
///   scheduler->executeBlock(block)   // BaselineScheduler (v2): executes via the EVM,
///                                    // computes roots + receipts into the block
///   scheduler->commitBlock(header)   // BaselineScheduler + Storage: persists the block
///                                    // (prewriteBlockToBuffer + mergeBackStorage)
///
/// This reuses the exact persistence path PBFT/sealer use on v2 chains, so
/// eth_getBlockByNumber / eth_getTransactionReceipt read the committed block tables.
///
/// With produceEmptyBlocks=false a block is only produced when the mempool yielded at least
/// one transaction (used by EEST fixture runs so the produced block environment matches the
/// fixture). Otherwise an empty block is produced every tick (simplest timed block production).
class SingleNodeConsensus : public std::enable_shared_from_this<SingleNodeConsensus>
{
public:
    using Ptr = std::shared_ptr<SingleNodeConsensus>;

    SingleNodeConsensus(std::shared_ptr<bcos::scheduler::SchedulerInterface> _scheduler,
        bcos::protocol::BlockFactory::Ptr _blockFactory, bcos::txpool::MemPoolImpl& _memPool,
        bcos::ledger::LedgerInterface::Ptr _ledger, std::uint32_t _compatibilityVersion,
        std::uint64_t _blockIntervalMs = 1000, bool _produceEmptyBlocks = true,
        bcos::crypto::HashType _prevRandao = bcos::crypto::HashType{},
        std::string _feeRecipient = "0x0000000000000000000000000000000000000000",
        std::uint64_t _fixedTimestamp = 0, std::uint64_t _gasLimit = 3000000000);

    ~SingleNodeConsensus();

    SingleNodeConsensus(SingleNodeConsensus const&) = delete;
    SingleNodeConsensus& operator=(SingleNodeConsensus const&) = delete;

    void start();
    void stop();
    bool running() const noexcept { return m_running.load(); }

private:
    void loop();
    void produceBlock();

    std::shared_ptr<bcos::scheduler::SchedulerInterface> m_scheduler;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    bcos::txpool::MemPoolImpl& m_memPool;
    bcos::ledger::LedgerInterface::Ptr m_ledger;

    std::uint32_t m_compatibilityVersion;
    std::uint64_t m_blockIntervalMs;
    bool m_produceEmptyBlocks;
    bcos::crypto::HashType m_prevRandao;
    std::string m_feeRecipient;
    std::uint64_t m_fixedTimestamp;
    std::uint64_t m_gasLimit;

    std::atomic_bool m_running = {false};
    std::thread m_thread;
};

}  // namespace bcos::single_consensus
