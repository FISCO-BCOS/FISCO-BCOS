/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "EngineServiceCommon.h"
#include "PayloadCache.h"

#include <bcos-framework/engine/Errors.h>
#include <bcos-framework/engine/Types.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>

#include <mutex>
#include <optional>
#include <shared_mutex>

namespace bcos::engine
{

struct TrackedHeadBlock
{
    h256 hash;
    bcos::protocol::BlockNumber blockNumber = 0;
};

struct ResolvedForkchoice
{
    ForkchoiceState state;
    bcos::protocol::BlockNumber headNumber;
    std::optional<bcos::protocol::BlockNumber> safeNumber;
    std::optional<bcos::protocol::BlockNumber> finalizedNumber;
    bool headCanonical;
    bool payloadAttributesPresent;
    /// op-geth forkchoiceUpdated: after SetCanonical(head), ReadCanonicalHash(number)
    /// must equal the submitted safe/finalized hash. Default true so unit fixtures that
    /// construct ResolvedForkchoice without a ledger still apply.
    bool safeCanonical = true;
    bool finalizedCanonical = true;
};

enum class ForkchoiceApplyResult
{
    Applied,
    Swallowed
};

class EngineTracker
{
public:
    class ExclusiveAccess;
    class SharedAccess;

    ForkchoiceApplyResult applyForkchoice(const ResolvedForkchoice& resolved);
    GetPayloadResult getPayload(const PayloadID& payloadId, std::uint32_t version) const;
    std::optional<TrackedHeadBlock> trackedHead() const;
    std::optional<bcos::protocol::BlockNumber> safeBlockNumber() const;
    std::optional<bcos::protocol::BlockNumber> finalizedBlockNumber() const;
    ExclusiveAccess lockExclusive();
    SharedAccess lockShared() const;

private:
    static bool isGetPayloadVersionSupported(std::uint32_t version);

    mutable std::shared_mutex m_mutex;
    ForkchoiceState m_forkchoiceState;
    std::optional<TrackedHeadBlock> m_trackedHead;
    std::optional<bcos::protocol::BlockNumber> m_safe;
    std::optional<bcos::protocol::BlockNumber> m_finalized;
    PayloadCache m_payloads;
};

class EngineTracker::ExclusiveAccess
{
public:
    ExclusiveAccess() = default;
    ExclusiveAccess(ExclusiveAccess&& other) noexcept
      : m_owner(other.m_owner), m_lock(std::move(other.m_lock))
    {
        other.m_owner = nullptr;
    }
    ExclusiveAccess& operator=(ExclusiveAccess&& other) noexcept
    {
        if (this != &other)
        {
            m_lock = std::move(other.m_lock);
            m_owner = other.m_owner;
            other.m_owner = nullptr;
        }
        return *this;
    }
    ExclusiveAccess(const ExclusiveAccess&) = delete;
    ExclusiveAccess& operator=(const ExclusiveAccess&) = delete;

    BuiltPayloadPtr findPayload(const PayloadID& id) const;
    std::optional<PayloadID> payloadIdForHash(const h256& blockHash) const;
    PayloadCache::PutResult putPayload(PayloadID id, h256 blockHash, BuiltPayloadPtr entry);
    PayloadCache::PutResult putUnboundedPayload(
        PayloadID id, h256 blockHash, BuiltPayloadPtr entry);
    PayloadCache::PutResult putAndRetainPayload(
        PayloadID id, h256 blockHash, BuiltPayloadPtr entry);
    void retainOnly(const PayloadID& id, const h256& blockHash);
    PayloadCache snapshotPayloadCache() const;
    void restorePayloadCache(PayloadCache cache);
    const ForkchoiceState& forkchoiceState() const;

private:
    friend class EngineTracker;
    explicit ExclusiveAccess(EngineTracker& owner) : m_owner(&owner), m_lock(owner.m_mutex) {}
    void requireOwner() const;
    EngineTracker* m_owner = nullptr;
    std::unique_lock<std::shared_mutex> m_lock;
};

class EngineTracker::SharedAccess
{
public:
    SharedAccess() = default;
    SharedAccess(SharedAccess&& other) noexcept
      : m_owner(other.m_owner), m_lock(std::move(other.m_lock))
    {
        other.m_owner = nullptr;
    }
    SharedAccess& operator=(SharedAccess&& other) noexcept
    {
        if (this != &other)
        {
            m_lock = std::move(other.m_lock);
            m_owner = other.m_owner;
            other.m_owner = nullptr;
        }
        return *this;
    }
    SharedAccess(const SharedAccess&) = delete;
    SharedAccess& operator=(const SharedAccess&) = delete;

    BuiltPayloadPtr findPayload(const PayloadID& id) const;
    std::optional<PayloadID> payloadIdForHash(const h256& blockHash) const;
    const ForkchoiceState& forkchoiceState() const;

private:
    friend class EngineTracker;
    explicit SharedAccess(const EngineTracker& owner) : m_owner(&owner), m_lock(owner.m_mutex) {}
    void requireOwner() const;
    const EngineTracker* m_owner = nullptr;
    std::shared_lock<std::shared_mutex> m_lock;
};

/// Publish a built payload with rollback-on-throw over the PayloadCache snapshot and
/// the caller-side artifacts map. Consolidated from the byte-identical eth_detail /
/// op_detail copies (review PR #5544); both engine services call this single template
/// so cache-rollback semantics cannot drift.
template <class ArtifactsMap, class ArtifactNode>
PayloadCache::PutResult publishBuiltPayload(EngineTracker::ExclusiveAccess& guard,
    ArtifactsMap& artifacts, PayloadID const& payloadId, h256 const& blockHash,
    BuiltPayloadPtr entry, ArtifactNode&& artifactNode)
{
    PayloadCache cacheRollback = guard.snapshotPayloadCache();
    ArtifactsMap artifactsRollback = artifacts;
    try
    {
        // Match release EngineServiceImpl: bounded FIFO (PayloadCache::put, cap 64).
        auto putResult = guard.putPayload(payloadId, blockHash, std::move(entry));
        artifacts[payloadId] = std::forward<ArtifactNode>(artifactNode);
        for (auto const& evictedId : putResult.evicted)
        {
            artifacts.erase(evictedId);
        }
        return putResult;
    }
    catch (...)
    {
        guard.restorePayloadCache(std::move(cacheRollback));
        artifacts = std::move(artifactsRollback);
        throw;
    }
}

}  // namespace bcos::engine
