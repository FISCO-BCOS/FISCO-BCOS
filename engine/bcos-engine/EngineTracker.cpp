/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "EngineTracker.h"

// Upstream pin: op-geth d401af16f2dd94b010a72eaef10e07ac10b31931
// (eth/catalyst/api.go forkchoiceUpdated / SetSafe / SetFinalized).

#include <bcos-utilities/Exceptions.h>
#include <stdexcept>

namespace bcos::engine
{

bool EngineTracker::isGetPayloadVersionSupported(std::uint32_t version)
{
    return version >= static_cast<std::uint32_t>(ApiVersion::V1) &&
           version <= static_cast<std::uint32_t>(ApiVersion::V5);
}

ForkchoiceApplyResult EngineTracker::applyForkchoice(const ResolvedForkchoice& resolved)
{
    const auto& headBlockNumber = resolved.headNumber;
    const auto& safeBlockNumber = resolved.safeNumber;
    const auto& finalizedBlockNumber = resolved.finalizedNumber;

    if (safeBlockNumber.has_value() && *safeBlockNumber > headBlockNumber)
    {
        BOOST_THROW_EXCEPTION(
            InvalidForkchoiceState{} << bcos::errinfo_comment{
                "Forkchoice safe block number must not exceed head block number"});
    }
    if (finalizedBlockNumber.has_value() && *finalizedBlockNumber > headBlockNumber)
    {
        BOOST_THROW_EXCEPTION(
            InvalidForkchoiceState{} << bcos::errinfo_comment{
                "Forkchoice finalized block number must not exceed head block number"});
    }
    if (finalizedBlockNumber.has_value() && safeBlockNumber.has_value() &&
        *finalizedBlockNumber > *safeBlockNumber)
    {
        BOOST_THROW_EXCEPTION(
            InvalidForkchoiceState{} << bcos::errinfo_comment{
                "Forkchoice finalized block number must not exceed safe block number"});
    }
    // Ancestor proxy for a linear ledger: number <= head (above) plus the resolver's
    // canonical-hash check. op-geth (eth/catalyst/api.go forkchoiceUpdated) rejects
    // "safe/final block not in canonical chain"; it does NOT reject a lower number
    // than the previously stored safe/finalized (SetSafe/SetFinalized overwrite).
    // The gate and the store below share this one predicate: a hash that is set but
    // whose number the resolver could not resolve is rejected here, so the store can
    // never observe that state and silently wipe a stored height.
    auto requiresCanonical = [](h256 const& hash,
                                 std::optional<bcos::protocol::BlockNumber> const& number) {
        return hash != bcos::h256{} && number.has_value();
    };
    if (resolved.state.safeBlockHash != bcos::h256{} && !safeBlockNumber.has_value())
    {
        BOOST_THROW_EXCEPTION(InvalidForkchoiceState{} << bcos::errinfo_comment{
                                  "Forkchoice safe block is set but its number could not be "
                                  "resolved"});
    }
    if (resolved.state.finalizedBlockHash != bcos::h256{} && !finalizedBlockNumber.has_value())
    {
        BOOST_THROW_EXCEPTION(InvalidForkchoiceState{} << bcos::errinfo_comment{
                                  "Forkchoice finalized block is set but its number could not "
                                  "be resolved"});
    }
    if (requiresCanonical(resolved.state.safeBlockHash, safeBlockNumber) && !resolved.safeCanonical)
    {
        BOOST_THROW_EXCEPTION(InvalidForkchoiceState{} << bcos::errinfo_comment{
                                  "Forkchoice safe block not in canonical chain"});
    }
    if (requiresCanonical(resolved.state.finalizedBlockHash, finalizedBlockNumber) &&
        !resolved.finalizedCanonical)
    {
        BOOST_THROW_EXCEPTION(InvalidForkchoiceState{} << bcos::errinfo_comment{
                                  "Forkchoice finalized block not in canonical chain"});
    }

    std::unique_lock lock(m_mutex);
    if (m_trackedHead.has_value())
    {
        auto const& trackedHeadBlock = *m_trackedHead;
        if (headBlockNumber < trackedHeadBlock.blockNumber)
        {
            // Match release EngineServiceImpl: any older head is swallowed (VALID without
            // payloadId). Rebuild-on-parent is intentionally not supported on this branch.
            return ForkchoiceApplyResult::Swallowed;
        }
        else if (headBlockNumber == trackedHeadBlock.blockNumber)
        {
            if (resolved.state.headBlockHash != trackedHeadBlock.hash && !resolved.headCanonical)
            {
                BOOST_THROW_EXCEPTION(
                    InvalidForkchoiceState{} << bcos::errinfo_comment{
                        "Forkchoice head block hash conflicts with tracked block number"});
            }
        }
        else if (headBlockNumber == trackedHeadBlock.blockNumber + 1)
        {
            // Fail closed like safe/finalized above: a +1 advance that the resolver
            // cannot confirm canonical must not become the tracked head (finding —
            // headCanonical was previously consulted only in the same-height branch).
            if (!resolved.headCanonical)
            {
                BOOST_THROW_EXCEPTION(InvalidForkchoiceState{} << bcos::errinfo_comment{
                                          "Forkchoice head block is not canonical"});
            }
        }
        else
        {
            BOOST_THROW_EXCEPTION(InvalidForkchoiceState{} << bcos::errinfo_comment{
                                      "Forkchoice head block number must increase by exactly 1"});
        }
    }
    else if (!resolved.headCanonical)
    {
        // First apply: same fail-closed rule — an unconfirmed head must not seed the
        // tracker, or every later +1/conflict check runs against a bogus tip.
        BOOST_THROW_EXCEPTION(InvalidForkchoiceState{} << bcos::errinfo_comment{
                                  "Forkchoice head block is not canonical"});
    }

    m_forkchoiceState = resolved.state;
    m_trackedHead = TrackedHeadBlock{
        .hash = resolved.state.headBlockHash,
        .blockNumber = headBlockNumber,
    };
    // Number rewind is legal (op-geth SetSafe/SetFinalized overwrite), but an
    // all-zero (Engine-API "not set") safe/finalized hash must NOT clear the stored
    // value: op-geth only calls SetSafe/SetFinalized for non-zero hashes (finding AJ).
    // Same requiresCanonical predicate as the gate above: only a fully resolved
    // (non-zero hash + present number) pair overwrites the stored height, so a
    // set-but-unresolved hash can never wipe it.
    if (requiresCanonical(resolved.state.safeBlockHash, safeBlockNumber))
    {
        m_safe = safeBlockNumber;
    }
    if (requiresCanonical(resolved.state.finalizedBlockHash, finalizedBlockNumber))
    {
        m_finalized = finalizedBlockNumber;
    }
    return ForkchoiceApplyResult::Applied;
}

GetPayloadResult EngineTracker::getPayload(const PayloadID& payloadId, std::uint32_t version) const
{
    if (!isGetPayloadVersionSupported(version))
    {
        BOOST_THROW_EXCEPTION(UnsupportedEngineApiVersion{}
                              << bcos::errinfo_comment{"Unsupported Engine API version"});
    }

    BuiltPayloadPtr entry;
    {
        std::shared_lock lock(m_mutex);
        entry = m_payloads.find(payloadId);
    }
    if (!entry)
    {
        BOOST_THROW_EXCEPTION(UnknownPayload{} << bcos::errinfo_comment{"Unknown payload"});
    }
    // Finding AF: BuiltPayload is immutable once published. Copy the shared_ptr
    // under the lock, then check shape and assemble GetPayloadData (tx raw /
    // blobs) outside so FCU publish / newPayload commit are not blocked.
    engine_common::requireGetPayloadShape(
        entry->version, entry->executionPayload, entry->parentBeaconBlockRoot, version);

    return std::make_unique<GetPayloadData>(GetPayloadData{
        .executionPayload = entry->executionPayload,
        .blockValue = entry->blockValue,
        .blobsBundle = entry->blobsBundle,
        .shouldOverrideBuilder = entry->shouldOverrideBuilder,
        .executionRequests = version >= static_cast<std::uint32_t>(ApiVersion::V4) ?
                                 std::optional<std::vector<bytes>>{std::in_place} :
                                 std::nullopt,
        .parentBeaconBlockRoot = entry->parentBeaconBlockRoot,
    });
}

std::optional<TrackedHeadBlock> EngineTracker::trackedHead() const
{
    std::shared_lock lock(m_mutex);
    return m_trackedHead;
}

std::optional<bcos::protocol::BlockNumber> EngineTracker::safeBlockNumber() const
{
    std::shared_lock lock(m_mutex);
    return m_safe;
}

std::optional<bcos::protocol::BlockNumber> EngineTracker::finalizedBlockNumber() const
{
    std::shared_lock lock(m_mutex);
    return m_finalized;
}

EngineTracker::ExclusiveAccess EngineTracker::lockExclusive()
{
    return ExclusiveAccess{*this};
}

EngineTracker::SharedAccess EngineTracker::lockShared() const
{
    return SharedAccess{*this};
}

void EngineTracker::ExclusiveAccess::requireOwner() const
{
    if (m_owner == nullptr || !m_lock.owns_lock())
    {
        BOOST_THROW_EXCEPTION(std::logic_error{"EngineTracker::ExclusiveAccess used after move"});
    }
}

void EngineTracker::SharedAccess::requireOwner() const
{
    if (m_owner == nullptr || !m_lock.owns_lock())
    {
        BOOST_THROW_EXCEPTION(std::logic_error{"EngineTracker::SharedAccess used after move"});
    }
}

BuiltPayloadPtr EngineTracker::ExclusiveAccess::findPayload(const PayloadID& id) const
{
    requireOwner();
    return m_owner->m_payloads.find(id);
}

std::optional<PayloadID> EngineTracker::ExclusiveAccess::payloadIdForHash(
    const h256& blockHash) const
{
    requireOwner();
    return m_owner->m_payloads.payloadIdForHash(blockHash);
}

PayloadCache::PutResult EngineTracker::ExclusiveAccess::putPayload(
    PayloadID id, h256 const& blockHash, BuiltPayloadPtr entry)
{
    requireOwner();
    return m_owner->m_payloads.put(std::move(id), blockHash, std::move(entry));
}

void EngineTracker::ExclusiveAccess::retainOnly(const PayloadID& id, const h256& blockHash)
{
    requireOwner();
    m_owner->m_payloads.retainOnly(id, blockHash);
}

void EngineTracker::ExclusiveAccess::erasePayload(const PayloadID& id)
{
    requireOwner();
    m_owner->m_payloads.erase(id);
}

PayloadCache::PutResult EngineTracker::ExclusiveAccess::putAndRetainPayload(
    PayloadID id, h256 const& blockHash, BuiltPayloadPtr entry)
{
    requireOwner();
    return m_owner->m_payloads.putAndRetainOnly(std::move(id), blockHash, std::move(entry));
}

PayloadCache EngineTracker::ExclusiveAccess::snapshotPayloadCache() const
{
    requireOwner();
    return m_owner->m_payloads.duplicate();
}

void EngineTracker::ExclusiveAccess::restorePayloadCache(PayloadCache cache)
{
    requireOwner();
    m_owner->m_payloads.publishFrom(std::move(cache));
}

ForkchoiceState EngineTracker::ExclusiveAccess::forkchoiceState() const
{
    requireOwner();
    // By value: a reference into the guarded member would outlive this guard's lock
    // and race a concurrent applyForkchoice writer.
    return m_owner->m_forkchoiceState;
}

BuiltPayloadPtr EngineTracker::SharedAccess::findPayload(const PayloadID& id) const
{
    requireOwner();
    return m_owner->m_payloads.find(id);
}

std::optional<PayloadID> EngineTracker::SharedAccess::payloadIdForHash(const h256& blockHash) const
{
    requireOwner();
    return m_owner->m_payloads.payloadIdForHash(blockHash);
}

ForkchoiceState EngineTracker::SharedAccess::forkchoiceState() const
{
    requireOwner();
    return m_owner->m_forkchoiceState;
}

}  // namespace bcos::engine
