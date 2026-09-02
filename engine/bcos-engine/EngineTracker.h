/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "PayloadCache.h"
#include "SplitEngineCommon.h"

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
    ExclusiveAccess(ExclusiveAccess&&) noexcept = default;
    ExclusiveAccess& operator=(ExclusiveAccess&&) noexcept = default;
    ExclusiveAccess(const ExclusiveAccess&) = delete;
    ExclusiveAccess& operator=(const ExclusiveAccess&) = delete;

    CommonPayloadEntryPtr findPayload(const PayloadID& id) const;
    std::optional<PayloadID> payloadIdForHash(const h256& blockHash) const;
    PayloadCache::PutResult putPayload(PayloadID id, h256 blockHash, CommonPayloadEntryPtr entry);
    PayloadCache::PutResult putUnboundedPayload(
        PayloadID id, h256 blockHash, CommonPayloadEntryPtr entry);
    PayloadCache::PutResult putAndRetainPayload(
        PayloadID id, h256 blockHash, CommonPayloadEntryPtr entry);
    void retainOnly(const PayloadID& id, const h256& blockHash);
    PayloadCache snapshotPayloadCache() const;
    void restorePayloadCache(PayloadCache cache) noexcept;
    const ForkchoiceState& forkchoiceState() const;

private:
    friend class EngineTracker;
    explicit ExclusiveAccess(EngineTracker& owner) : m_owner(&owner), m_lock(owner.m_mutex) {}
    EngineTracker* m_owner = nullptr;
    std::unique_lock<std::shared_mutex> m_lock;
};

class EngineTracker::SharedAccess
{
public:
    SharedAccess() = default;
    SharedAccess(SharedAccess&&) noexcept = default;
    SharedAccess& operator=(SharedAccess&&) noexcept = default;
    SharedAccess(const SharedAccess&) = delete;
    SharedAccess& operator=(const SharedAccess&) = delete;

    CommonPayloadEntryPtr findPayload(const PayloadID& id) const;
    std::optional<PayloadID> payloadIdForHash(const h256& blockHash) const;
    const ForkchoiceState& forkchoiceState() const;

private:
    friend class EngineTracker;
    explicit SharedAccess(const EngineTracker& owner) : m_owner(&owner), m_lock(owner.m_mutex) {}
    const EngineTracker* m_owner = nullptr;
    std::shared_lock<std::shared_mutex> m_lock;
};

}  // namespace bcos::engine
