/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "EngineTracker.h"

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
    if (safeBlockNumber.has_value() && !resolved.safeCanonical)
    {
        BOOST_THROW_EXCEPTION(InvalidForkchoiceState{} << bcos::errinfo_comment{
                                  "Forkchoice safe block not in canonical chain"});
    }
    if (finalizedBlockNumber.has_value() && !resolved.finalizedCanonical)
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
        else if (headBlockNumber != trackedHeadBlock.blockNumber + 1)
        {
            BOOST_THROW_EXCEPTION(InvalidForkchoiceState{} << bcos::errinfo_comment{
                                      "Forkchoice head block number must increase by exactly 1"});
        }
    }

    m_forkchoiceState = resolved.state;
    m_trackedHead = TrackedHeadBlock{
        .hash = resolved.state.headBlockHash,
        .blockNumber = headBlockNumber,
    };
    // Number rewind is legal (op-geth SetSafe/SetFinalized overwrite), but an
    // all-zero (Engine-API "not set") safe/finalized hash must NOT clear the stored
    // value: op-geth only calls SetSafe/SetFinalized for non-zero hashes (finding AJ).
    if (resolved.state.safeBlockHash != bcos::h256{})
    {
        m_safe = safeBlockNumber;
    }
    if (resolved.state.finalizedBlockHash != bcos::h256{})
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

    std::shared_lock lock(m_mutex);
    auto entry = m_payloads.find(payloadId);
    if (!entry)
    {
        BOOST_THROW_EXCEPTION(UnknownPayload{} << bcos::errinfo_comment{"Unknown payload"});
    }
    if (!engine_common::isGetPayloadVersionCompatible(
            static_cast<ApiVersion>(version), entry->version))
    {
        BOOST_THROW_EXCEPTION(IncompatiblePayloadVersion{} << bcos::errinfo_comment{
                                  "Payload version is incompatible with requested method version"});
    }
    if (version >= static_cast<std::uint32_t>(ApiVersion::V4) &&
        !entry->executionPayload.withdrawalsRoot.has_value())
    {
        BOOST_THROW_EXCEPTION(IncompatiblePayloadVersion{} << bcos::errinfo_comment{
                                  "Payload does not carry the V4+ response shape"});
    }
    // V3 responses must render the blob-gas pair and the beacon root; a V3-tagged entry
    // that somehow lacks them would serialize an incomplete wire shape (finding AN).
    if (version >= static_cast<std::uint32_t>(ApiVersion::V3) &&
        version < static_cast<std::uint32_t>(ApiVersion::V4) &&
        (!entry->executionPayload.blobGasUsed.has_value() ||
            !entry->executionPayload.excessBlobGas.has_value() ||
            !entry->parentBeaconBlockRoot.has_value()))
    {
        BOOST_THROW_EXCEPTION(IncompatiblePayloadVersion{} << bcos::errinfo_comment{
                                  "Payload does not carry the V3+ response shape"});
    }

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
    PayloadID id, h256 blockHash, BuiltPayloadPtr entry)
{
    requireOwner();
    return m_owner->m_payloads.put(std::move(id), blockHash, std::move(entry));
}

PayloadCache::PutResult EngineTracker::ExclusiveAccess::putUnboundedPayload(
    PayloadID id, h256 blockHash, BuiltPayloadPtr entry)
{
    requireOwner();
    return m_owner->m_payloads.putUnbounded(std::move(id), blockHash, std::move(entry));
}

void EngineTracker::ExclusiveAccess::retainOnly(const PayloadID& id, const h256& blockHash)
{
    requireOwner();
    m_owner->m_payloads.retainOnly(id, blockHash);
}

PayloadCache::PutResult EngineTracker::ExclusiveAccess::putAndRetainPayload(
    PayloadID id, h256 blockHash, BuiltPayloadPtr entry)
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

const ForkchoiceState& EngineTracker::ExclusiveAccess::forkchoiceState() const
{
    requireOwner();
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

const ForkchoiceState& EngineTracker::SharedAccess::forkchoiceState() const
{
    requireOwner();
    return m_owner->m_forkchoiceState;
}

}  // namespace bcos::engine
