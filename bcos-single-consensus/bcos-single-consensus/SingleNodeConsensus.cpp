/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *
 * @file SingleNodeConsensus.cpp
 * @brief Built-in single-node consensus driver (定时出块).
 */

#include "SingleNodeConsensus.h"
#include "bcos-framework/engine/Types.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-task/Wait.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Common.h"
#include <boost/exception/diagnostic_information.hpp>
#include <chrono>
#include <future>

using namespace bcos;
using namespace bcos::single_consensus;

#define SINGLE_CONSENSUS_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("SINGLE_CONSENSUS")

namespace
{
/// Bridges the callback-style ledger API to a blocking wait for the driver thread, with two
/// guards the raw promise/future pattern lacks:
///  * setValue() is idempotent via std::call_once — a double-invoked callback cannot throw
///    std::future_error(promise_already_satisfied) from the ledger thread, where nothing
///    catches it and the process would std::terminate.
///  * wait() polls and aborts as soon as the driver is stopping, so stop()'s join() cannot
///    hang forever on a callback that never arrives.
/// The callback lambda holds a shared_ptr, so even if wait() bails out (shutdown) and the
/// call site returns, a later callback invocation still targets a live promise.
template <typename T>
class CallbackPromise
{
public:
    void setValue(T value)
    {
        std::call_once(
            m_once, [this, v = std::move(value)]() mutable { m_promise.set_value(std::move(v)); });
    }

    T wait(std::atomic_bool const& _running,
        std::chrono::milliseconds _pollInterval = std::chrono::milliseconds(50))
    {
        auto future = m_promise.get_future();
        while (_running.load())
        {
            if (future.wait_for(_pollInterval) == std::future_status::ready)
            {
                return future.get();
            }
        }
        throw std::runtime_error("SingleNodeConsensus stopped while waiting for an async callback");
    }

private:
    std::once_flag m_once;
    std::promise<T> m_promise;
};
}  // namespace

namespace
{
/// Karst Engine dialect: the built-in CL speaks the same method versions op-node uses
/// against a Karst chain (rollup/types.go version selection) — forkchoiceUpdated V3 to
/// build, getPayload V5 to fetch, newPayload V4 to commit. The produced blocks' execution
/// semantics are still governed by the ledger's executor version, not by these versions.
constexpr std::uint32_t c_forkchoiceVersion =
    static_cast<std::uint32_t>(bcos::engine::ApiVersion::V3);
constexpr std::uint32_t c_getPayloadVersion =
    static_cast<std::uint32_t>(bcos::engine::ApiVersion::V5);
constexpr std::uint32_t c_newPayloadVersion =
    static_cast<std::uint32_t>(bcos::engine::ApiVersion::V4);
}  // namespace

SingleNodeConsensus::SingleNodeConsensus(bcos::engine::AnyEngineService& _engineService,
    bcos::ledger::LedgerInterface::Ptr _ledger, std::uint64_t _blockIntervalMs,
    bool _produceEmptyBlocks, bcos::crypto::HashType _prevRandao, std::string _feeRecipient,
    std::uint64_t _fixedTimestamp)
  : m_engineService(_engineService),
    m_ledger(std::move(_ledger)),
    m_blockIntervalMs(_blockIntervalMs > 0 ? _blockIntervalMs : 1000),
    m_produceEmptyBlocks(_produceEmptyBlocks),
    m_prevRandao(_prevRandao),
    // Parse the coinbase exactly once here: a malformed fee_recipient must fail the node at
    // startup, not fail on the first block tick.
    m_feeRecipient(toAddress(_feeRecipient)),
    m_fixedTimestamp(_fixedTimestamp)
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
                               << LOG_KV("produceEmptyBlocks", m_produceEmptyBlocks);
    m_thread = std::thread([this] { loop(); });
}

void SingleNodeConsensus::stop()
{
    if (!m_running.exchange(false))
    {
        return;
    }
    // Wake the loop's interval wait so a large block_interval does not stall shutdown.
    m_cv.notify_all();
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
            SINGLE_CONSENSUS_LOG(ERROR)
                << LOG_DESC("produceBlock iteration threw (unknown)")
                << LOG_KV("diagnostic",
                    boost::current_exception_diagnostic_information());
        }
        // Drain the mempool as fast as possible when transactions are available (a tx is
        // sealed immediately after submission instead of waiting for the next interval
        // tick — the EEST harness runs one tx per unit, so this removes ~1s/unit of
        // latency). Only pace the loop when nothing was sealed (empty block or skipped).
        // stop() notifies the condition variable so shutdown is prompt even with a large
        // block_interval.
        if (!sealedTxBlock)
        {
            std::unique_lock lock(m_cvMutex);
            m_cv.wait_for(lock, std::chrono::milliseconds(m_blockIntervalMs),
                [this] { return !m_running.load(); });
        }
    }
}

void SingleNodeConsensus::resolveInitialHead()
{
    auto headPromise =
        std::make_shared<CallbackPromise<std::tuple<Error::Ptr, protocol::BlockNumber>>>();
    m_ledger->asyncGetBlockNumber([headPromise](Error::Ptr _error, protocol::BlockNumber _number) {
        headPromise->setValue(std::make_tuple(std::move(_error), _number));
    });
    auto [headError, headNumber] = headPromise->wait(m_running);
    if (headError || headNumber < 0)
    {
        SINGLE_CONSENSUS_LOG(ERROR) << LOG_DESC("resolve head block number failed")
                                    << LOG_KV("msg", headError ? headError->errorMessage() : "");
        BOOST_THROW_EXCEPTION(
            std::runtime_error("SingleNodeConsensus: cannot resolve the initial head"));
    }
    m_headNumber = headNumber;
    m_headHash = task::syncWait(ledger::getBlockHash(*m_ledger, headNumber));

    // Seed m_lastTimestamp from the head header's timestamp (whole-second milliseconds)
    // so a restart in the same wall-clock second as the parent cannot reseal
    // parent.timestamp: EIP-2 requires strictly increasing block timestamps, and
    // produceBlock only steps +1000ms from m_lastTimestamp. Without the seed, a restart
    // within the parent block's second makes the first produced block share the parent's
    // timestamp. A legacy genesis without [eth_genesis_header] has timestamp 0, which
    // seeds to the same default and changes nothing.
    auto headerPromise =
        std::make_shared<CallbackPromise<std::tuple<Error::Ptr, protocol::Block::Ptr>>>();
    m_ledger->asyncGetBlockDataByNumber(headNumber, ledger::HEADER,
        [headerPromise](Error::Ptr _error, protocol::Block::Ptr _block) {
            headerPromise->setValue(std::make_tuple(std::move(_error), std::move(_block)));
        });
    auto [headHeaderError, headBlock] = headerPromise->wait(m_running);
    if (!headHeaderError && headBlock && headBlock->blockHeader())
    {
        m_lastTimestamp =
            static_cast<std::uint64_t>(headBlock->blockHeader()->timestamp());
    }

    m_headInitialized = true;
    SINGLE_CONSENSUS_LOG(INFO) << LOG_DESC("Resolved initial head")
                               << LOG_KV("number", m_headNumber)
                               << LOG_KV("hash", m_headHash.hexPrefixed())
                               << LOG_KV("lastTimestampMs", m_lastTimestamp);
}

bool SingleNodeConsensus::produceBlock()
{
    if (!m_headInitialized)
    {
        resolveInitialHead();
    }

    // Assemble the payload attributes — the "block proposal" the CL hands to the execution
    // layer. Header timestamps are in milliseconds (the executor divides by 1000 to get
    // seconds, matching EEST's currentTimestamp unit). fixed_timestamp (seconds) is pinned by
    // the harness so the produced block's timestamp matches the fixture; 0 = wall clock.
    //
    // Ethereum block timestamps are second-granular: BlockHeader stores milliseconds, so the
    // value must be a whole-second multiple — the Eth RLP bridge (EthBlockHeader ctor) rejects
    // sub-second ms, and the produced header is hashed as an Eth header. EIP-2 requires
    // strictly increasing block timestamps, so both modes round down to a whole second and
    // then enforce monotonicity in whole-second steps (a fixed timestamp is used verbatim for
    // the first block so EEST's expected block.timestamp == currentTimestamp holds; later
    // blocks step +1s).
    //
    // utcTime() already returns milliseconds (bcos-utilities/Common.cpp, despite the header
    // comment saying "seconds") — do NOT multiply by 1000, which would make the block
    // timestamp ~1.786e15 -> year 58577 in the EVM (block.timestamp / base fee schedules etc).
    auto const nowMs = static_cast<std::uint64_t>(utcTime());
    auto const wholeSecondMs = [&]() -> std::uint64_t {
        if (m_fixedTimestamp > 0)
        {
            return m_fixedTimestamp * 1000;
        }
        return nowMs / 1000 * 1000;
    }();
    std::uint64_t const timestamp = std::max(wholeSecondMs, m_lastTimestamp + 1000);
    m_lastTimestamp = timestamp;
    bcos::engine::PayloadAttributes payloadAttributes;
    payloadAttributes.prevRandao = m_prevRandao;
    payloadAttributes.suggestedFeeRecipient = m_feeRecipient;
    payloadAttributes.timestamp = timestamp;
    // V3 attributes require withdrawals and parentBeaconBlockRoot. This CL has no beacon
    // chain and OP L2 has no withdrawals, so both are the fixed empty/zero values. The
    // structs are passed in-process (behind the RPC boundary), so the timestamp above
    // stays in the internal millisecond unit — the Engine wire's seconds<->ms conversion
    // lives in the RPC serialization layer only.
    //
    // TODO(C4 header fields): the zero parentBeaconBlockRoot here, and the zero
    // withdrawalsRoot the EngineService stamps onto the payload it builds from these
    // attributes (see the placeholder note in EngineServiceImpl.h buildPayload), are
    // built-in-CL stand-ins, NOT production semantics. Because they are zero, a payload
    // produced by this driver is byte-indistinguishable from one a broken or malicious
    // external CL would submit with zero roots. Do not read this file as evidence that
    // zero roots are acceptable on a real chain.
    payloadAttributes.withdrawals = std::vector<bcos::engine::WithdrawalV1>{};
    payloadAttributes.parentBeaconBlockRoot = bcos::h256{};

    // forkchoiceUpdated(head, attributes): the EL resolves the head hash from storage, removes
    // stale transactions, seals the in-process mempool (with the nonce-vs-state check) and
    // builds + executes the payload — the step that previously duplicated the mempool /
    // scheduler logic inside this driver. The head passed here is the last block this CL
    // committed, so the EL's tracked-head validation (must increase by exactly 1) holds.
    bcos::engine::ForkchoiceState forkchoiceState{
        .headBlockHash = m_headHash,
        .safeBlockHash = m_headHash,
        .finalizedBlockHash = m_headHash,
    };
    auto fcResult = task::syncWait(
        m_engineService.updateForkchoice(forkchoiceState, &payloadAttributes, c_forkchoiceVersion));
    if (fcResult.payloadStatus.status != bcos::engine::PayloadValidationStatus::Valid ||
        !fcResult.payloadId)
    {
        SINGLE_CONSENSUS_LOG(ERROR)
            << LOG_DESC("updateForkchoice failed to build a payload")
            << LOG_KV("status", static_cast<int>(fcResult.payloadStatus.status))
            << LOG_KV("error",
                   fcResult.payloadStatus.validationError.value_or("no payloadId returned"));
        return false;
    }

    // getPayload: fetch the built block proposal (sealed transactions + header).
    auto payload =
        task::syncWait(m_engineService.getPayload(*fcResult.payloadId, c_getPayloadVersion));
    if (!payload)
    {
        SINGLE_CONSENSUS_LOG(ERROR) << LOG_DESC("getPayload returned null");
        return false;
    }
    auto& executionPayload = payload->executionPayload;
    bool const sealedTxBlock = !executionPayload.transactions.empty();

    // produceEmptyBlocks=false: only produce a block that carries at least one transaction
    // (used by EEST fixture runs so the produced block environment matches the fixture).
    if (executionPayload.transactions.empty() && !m_produceEmptyBlocks)
    {
        SINGLE_CONSENSUS_LOG(DEBUG) << LOG_DESC("Skip empty block (produceEmptyBlocks=false)");
        return false;
    }

    // newPayload: validate + execute + commit the built block to storage. Because the payload
    // was built locally, the EngineService also commits its state view here (pushView +
    // mergeBackStorage), keeping SYS_NUMBER_2_HASH / SYS_HASH_2_NUMBER current for the ledger
    // and the eth_* RPC reads.
    bcos::engine::NewPayloadRequest request;
    request.executionPayload = std::move(executionPayload);
    // newPayloadV4: echo the beacon root the payload was built with (as op-node does)
    // and pass the required-empty blob-hash / executionRequests lists.
    request.parentBeaconBlockRoot = payload->parentBeaconBlockRoot;
    request.executionRequests = std::vector<bcos::bytes>{};
    auto newPayloadStatus =
        task::syncWait(m_engineService.newPayload(request, c_newPayloadVersion));
    if (newPayloadStatus.status != bcos::engine::PayloadValidationStatus::Valid)
    {
        SINGLE_CONSENSUS_LOG(ERROR)
            << LOG_DESC("newPayload failed")
            << LOG_KV("status", static_cast<int>(newPayloadStatus.status))
            << LOG_KV("error", newPayloadStatus.validationError.value_or(""));
        return false;
    }

    // Advance the CL-side head to the newly committed block. The EngineService does not expose
    // or advance the canonical head, so the CL tracks it — exactly like a real consensus layer
    // tracks its own fork choice.
    m_headNumber = request.executionPayload.blockNumber;
    m_headHash = request.executionPayload.blockHash;

    SINGLE_CONSENSUS_LOG(INFO) << LOG_DESC("Committed block")
                               << LOG_KV("number", request.executionPayload.blockNumber)
                               << LOG_KV("hash", request.executionPayload.blockHash.hexPrefixed())
                               << LOG_KV("txs", request.executionPayload.transactions.size())
                               << LOG_KV("gasUsed", request.executionPayload.gasUsed.str())
                               << LOG_KV("stateRoot",
                                      request.executionPayload.stateRoot.hexPrefixed());
    return sealedTxBlock;
}
