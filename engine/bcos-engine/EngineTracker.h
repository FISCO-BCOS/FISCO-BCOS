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

// TrackedHeadBlock now lives in EngineServiceCommon.h (one shared definition).

struct ResolvedForkchoice
{
    ForkchoiceState state;
    bcos::protocol::BlockNumber headNumber;
    std::optional<bcos::protocol::BlockNumber> safeNumber;
    std::optional<bcos::protocol::BlockNumber> finalizedNumber;
    bool headCanonical = false;
    bool payloadAttributesPresent = false;
    /// op-geth forkchoiceUpdated: after SetCanonical(head), ReadCanonicalHash(number)
    /// must equal the submitted safe/finalized hash. Default false: a resolver that
    /// omits the flags must not fail-open a non-canonical safe/finalized.
    bool safeCanonical = false;
    bool finalizedCanonical = false;
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
    /// RAII guards over m_mutex. Unlock must run on the locking thread
    /// (shared_mutex); do not move a live guard across threads or hold it
    /// across co_await. applyForkchoice/getPayload take the same mutex —
    /// do not call them while a guard is held (non-recursive, deadlock).
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

/// unique_lock wrapper. Same-thread unlock only; never hold across co_await.
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
    ~ExclusiveAccess() = default;
    ExclusiveAccess(const ExclusiveAccess&) = delete;
    ExclusiveAccess& operator=(const ExclusiveAccess&) = delete;

    BuiltPayloadPtr findPayload(const PayloadID& id) const;
    std::optional<PayloadID> payloadIdForHash(const h256& blockHash) const;
    PayloadCache::PutResult putPayload(PayloadID id, h256 const& blockHash, BuiltPayloadPtr entry);
    PayloadCache::PutResult putAndRetainPayload(
        PayloadID id, h256 const& blockHash, BuiltPayloadPtr entry);
    void retainOnly(const PayloadID& id, const h256& blockHash);
    void erasePayload(PayloadID const& id);
    PayloadCache snapshotPayloadCache() const;
    void restorePayloadCache(PayloadCache cache);
    ForkchoiceState forkchoiceState() const;

private:
    friend class EngineTracker;
    explicit ExclusiveAccess(EngineTracker& owner) : m_owner(&owner), m_lock(owner.m_mutex) {}
    void requireOwner() const;
    EngineTracker* m_owner = nullptr;
    std::unique_lock<std::shared_mutex> m_lock;
};

/// shared_lock wrapper. Same-thread unlock only; never hold across co_await.
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
    ~SharedAccess() = default;
    SharedAccess(const SharedAccess&) = delete;
    SharedAccess& operator=(const SharedAccess&) = delete;

    BuiltPayloadPtr findPayload(const PayloadID& id) const;
    std::optional<PayloadID> payloadIdForHash(const h256& blockHash) const;
    ForkchoiceState forkchoiceState() const;

private:
    friend class EngineTracker;
    explicit SharedAccess(const EngineTracker& owner) : m_owner(&owner), m_lock(owner.m_mutex) {}
    void requireOwner() const;
    const EngineTracker* m_owner = nullptr;
    std::shared_lock<std::shared_mutex> m_lock;
};

/// Publish a built payload. put() is CoW, so a throw there leaves the live
/// cache unchanged. After a successful put, an artifacts insert throw must
/// restore the pre-put cache and artifacts — erasePayload(id) would drop a
/// replaced entry (same id, different payload) and its prior artifact.
/// Consolidated from the eth_detail / op_detail copies (review PR #5544).
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
