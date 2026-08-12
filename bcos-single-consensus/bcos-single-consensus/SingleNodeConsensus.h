/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file SingleNodeConsensus.h
 * @brief Built-in single-node consensus driver (定时出块).
 */

#pragma once

#include "bcos-framework/engine/AnyEngineService.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/FixedBytes.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace bcos::single_consensus
{
/// Built-in single-node consensus driver.
///
/// Acts as a minimal consensus-layer (CL) node: block production is driven entirely through
/// the EngineService interface — the same Engine API an external CL (op-node/Lodestar) uses —
/// instead of assembling block proposals and driving the mempool/scheduler directly (logic
/// that duplicated EngineServiceImpl):
///
///   engineService.updateForkchoice(head, payloadAttributes)  // seal mempool + build payload
///   engineService.getPayload(payloadId)                      // fetch the built payload
///   engineService.newPayload(request)                        // execute + commit to storage
///
/// The EngineService owns the mempool (remove/seal), the EVM scheduler and the storage commit
/// (pushView + mergeBackStorage), so this driver holds no scheduler/mempool dependency. It
/// only resolves the initial head from the ledger once at startup and then tracks the head
/// itself, exactly like a real consensus layer tracks its own fork choice.
///
/// With produceEmptyBlocks=false a block is only produced when the sealed payload carried at
/// least one transaction (used by EEST fixture runs so the produced block environment matches
/// the fixture). Otherwise an empty block is produced every tick (simplest timed block
/// production).
class SingleNodeConsensus : public std::enable_shared_from_this<SingleNodeConsensus>
{
public:
    using Ptr = std::shared_ptr<SingleNodeConsensus>;

    SingleNodeConsensus(bcos::engine::AnyEngineService& _engineService,
        bcos::ledger::LedgerInterface::Ptr _ledger, std::uint64_t _blockIntervalMs = 1000,
        bool _produceEmptyBlocks = true,
        bcos::crypto::HashType _prevRandao = bcos::crypto::HashType{},
        std::string _feeRecipient = "0x0000000000000000000000000000000000000000",
        std::uint64_t _fixedTimestamp = 0, std::uint32_t _engineApiVersion = 0);

    ~SingleNodeConsensus();

    SingleNodeConsensus(SingleNodeConsensus const&) = delete;
    SingleNodeConsensus& operator=(SingleNodeConsensus const&) = delete;

    void start();
    void stop();
    bool running() const noexcept { return m_running.load(); }

private:
    void loop();
    /// Produce one block through the EngineService. Returns true if a block carrying at least
    /// one transaction was sealed (the caller then immediately checks for more work), false
    /// otherwise (empty block or skipped — the caller sleeps @p m_blockIntervalMs before the
    /// next tick).
    bool produceBlock();
    /// Resolve the current committed head (number + hash) from the ledger once at startup.
    void resolveInitialHead();

    bcos::engine::AnyEngineService& m_engineService;
    bcos::ledger::LedgerInterface::Ptr m_ledger;

    std::uint64_t m_blockIntervalMs;
    bool m_produceEmptyBlocks;
    bcos::crypto::HashType m_prevRandao;
    /// Coinbase, parsed once in the constructor (a malformed [consensus] fee_recipient fails
    /// at startup instead of failing on the first block tick).
    bcos::Address m_feeRecipient;
    std::uint64_t m_fixedTimestamp;
    /// Engine API version this CL speaks (V1 default; the OP composition root passes V4).
    std::uint32_t m_engineApiVersion;

    /// CL-side head tracking. newPayload() persists the ledger block tables — including
    /// SYS_CURRENT_STATE / SYS_KEY_CURRENT_NUMBER — via ledger::prewriteBlockToBuffer
    /// (EngineServiceImpl.h, same FIB-104 pattern as the legacy commit path), so the head is
    /// fully recoverable from the ledger after a restart. The EngineService does not expose
    /// the canonical head, so it is resolved once at startup and tracked here afterwards,
    /// exactly like a real consensus layer tracks its own fork choice.
    bcos::protocol::BlockNumber m_headNumber = 0;
    bcos::crypto::HashType m_headHash;
    bool m_headInitialized = false;
    /// Timestamp of the last produced block (ms). EIP-2 requires strictly increasing block
    /// timestamps, so wall-clock mode never goes backwards relative to this value.
    std::uint64_t m_lastTimestamp = 0;

    std::atomic_bool m_running = {false};
    std::thread m_thread;
    std::mutex m_cvMutex;
    std::condition_variable m_cv;
};

}  // namespace bcos::single_consensus
