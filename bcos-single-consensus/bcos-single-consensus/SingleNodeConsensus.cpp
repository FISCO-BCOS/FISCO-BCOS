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
    std::uint64_t _fixedTimestamp, std::optional<std::uint64_t> _gasLimit,
    std::optional<bcos::bytes> _eip1559Params, std::optional<std::uint64_t> _minBaseFee)
  : m_engineService(_engineService),
    m_ledger(std::move(_ledger)),
    m_blockIntervalMs(_blockIntervalMs > 0 ? _blockIntervalMs : 1000),
    m_produceEmptyBlocks(_produceEmptyBlocks),
    m_prevRandao(_prevRandao),
    // Parse the coinbase exactly once here: a malformed fee_recipient must fail the node at
    // startup, not fail on the first block tick.
    m_feeRecipient(toAddress(_feeRecipient)),
    m_fixedTimestamp(_fixedTimestamp),
    m_gasLimit(_gasLimit),
    m_eip1559Params(std::move(_eip1559Params)),
    m_minBaseFee(_minBaseFee)
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
        // Wall-clock arm: the block timestamp advances in whole-second steps
        // (max(nowWholeSecondMs, m_lastTimestamp + 1000)), so a fast drain must NOT outrun
        // the wall clock — otherwise chain time drifts ahead and a restart can no longer
        // produce a timestamp strictly greater than the head (EIP-2). Wait until the wall
        // clock reaches m_lastTimestamp + 1000 even when a tx block was just sealed.
        // The fixed-timestamp (EEST) arm never consults the wall clock, so it still drains
        // as fast as possible (its monotonicity comes from m_headNumber, not time).
        // stop() notifies the condition variable so shutdown is prompt even with a large
        // block_interval.
        if (m_fixedTimestamp == 0)
        {
            auto const nowMs = static_cast<std::uint64_t>(utcTime());
            auto const targetMs = m_lastTimestamp + 1000;
            if (nowMs < targetMs)
            {
                std::unique_lock lock(m_cvMutex);
                m_cv.wait_for(lock, std::chrono::milliseconds(targetMs - nowMs),
                    [this] { return !m_running.load(); });
            }
        }
        else if (!sealedTxBlock)
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
    // Seed m_lastTimestamp from the head header so a restart cannot propose a timestamp
    // behind the head (runOpNewPayloadSteps / EIP-2 reject non-increasing timestamps).
    // A restart in the same wall-clock second as the parent would otherwise reseal
    // parent.timestamp. Header timestamps are whole-second milliseconds.
    auto headerPromise =
        std::make_shared<CallbackPromise<std::tuple<Error::Ptr, protocol::Block::Ptr>>>();
    m_ledger->asyncGetBlockDataByNumber(headNumber, ledger::HEADER,
        [headerPromise](Error::Ptr _error, protocol::Block::Ptr _block) {
            headerPromise->setValue(std::make_tuple(std::move(_error), std::move(_block)));
        });
    auto [headHeaderError, headBlock] = headerPromise->wait(m_running);
    if (!headHeaderError && headBlock && headBlock->blockHeader())
    {
        m_lastTimestamp = static_cast<std::uint64_t>(headBlock->blockHeader()->timestamp());
        SINGLE_CONSENSUS_LOG(INFO)
            << LOG_DESC("Seeded last timestamp from head") << LOG_KV("number", m_headNumber)
            << LOG_KV("timestampMs", m_lastTimestamp);
    }
    else
    {
        SINGLE_CONSENSUS_LOG(WARNING)
            << LOG_DESC("Could not seed last timestamp from head")
            << LOG_KV("err", headHeaderError ? headHeaderError->errorMessage() : "no header");
    }
    m_headInitialized = true;
    SINGLE_CONSENSUS_LOG(INFO) << LOG_DESC("Resolved initial head")
                               << LOG_KV("number", m_headNumber)
                               << LOG_KV("hash", m_headHash.hexPrefixed())
                               << LOG_KV("lastTimestampMs", m_lastTimestamp);
}

std::uint64_t SingleNodeConsensus::nextBlockTimestamp(std::uint64_t fixedTimestamp,
    std::uint64_t headNumber, std::uint64_t lastTimestamp, std::uint64_t nowMs)
{
    // utcTime() returns milliseconds; floor to a whole second and keep monotonicity by
    // advancing in whole-second steps (EIP-2 + EthBlockHeader whole-second requirement).
    std::uint64_t const nowWholeSecondMs = nowMs - nowMs % 1000;
    return fixedTimestamp > 0 ? (fixedTimestamp + headNumber) * 1000 :
                                std::max(nowWholeSecondMs, lastTimestamp + 1000);
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
    // Ethereum timestamps are second-granular (BlockHeader stores ms). EthBlockHeader
    // rejects sub-second ms; EIP-2 requires strictly increasing timestamps. Floor to a
    // whole second and advance in whole-second steps. The fixed-timestamp (EEST) arm is
    // (fixed + headNumber) * 1000 so later blocks still step +1s without consulting the
    // wall clock.
    // utcTime() already returns milliseconds (bcos-utilities/Common.cpp, despite the header
    // comment saying "seconds") — do NOT multiply by 1000, which would make the block
    // timestamp ~1.786e15 -> year 58577 in the EVM (block.timestamp / base fee schedules etc).
    auto const nowMs = static_cast<std::uint64_t>(utcTime());
    std::uint64_t const timestamp = nextBlockTimestamp(
        m_fixedTimestamp, static_cast<std::uint64_t>(m_headNumber), m_lastTimestamp, nowMs);
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
    // OP mode (FCU V3+, set by Initializer): gasLimit and Holocene eip1559Params are
    // mandatory on the OP path (validateOpPayloadAttributes). The generic V1 driver keeps
    // both absent — pre-V3 attributes must not carry them.
    if (m_gasLimit.has_value())
    {
        payloadAttributes.gasLimit = m_gasLimit;
    }
    if (m_eip1559Params.has_value())
    {
        payloadAttributes.eip1559Params = m_eip1559Params;
    }
    // Jovian makes minBaseFee mandatory on OP attributes (validateOpPayloadAttributes); the
    // OP arm supplies it whenever feature_op_jovian is active (0 = no floor) so the driver's
    // FCU attributes never stall block production after the fork. Pre-Jovian / generic V1
    // drivers keep it absent (minBaseFee must be null before Jovian).
    if (m_minBaseFee.has_value())
    {
        payloadAttributes.minBaseFee = m_minBaseFee;
    }

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
    // OP buildOpPayload stores envelopes only in rawTransactions and leaves
    // transactions[] empty; the generic path is the reverse. Count whichever
    // carrier the payload actually filled.
    auto const payloadTxCount = executionPayload.rawTransactions.has_value() ?
                                    executionPayload.rawTransactions->size() :
                                    executionPayload.transactions.size();
    bool const sealedTxBlock = payloadTxCount > 0;

    // produceEmptyBlocks=false: only produce a block that carries at least one transaction
    // (used by EEST fixture runs so the produced block environment matches the fixture).
    if (payloadTxCount == 0 && !m_produceEmptyBlocks)
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
                               << LOG_KV("txs", payloadTxCount)
                               << LOG_KV("gasUsed", request.executionPayload.gasUsed.str())
                               << LOG_KV("stateRoot",
                                      request.executionPayload.stateRoot.hexPrefixed());
    return sealedTxBlock;
}
