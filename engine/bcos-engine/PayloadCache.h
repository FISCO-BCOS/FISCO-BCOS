/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "EngineServiceCommon.h"

#include <bcos-framework/engine/Types.h>
#include <bcos-framework/protocol/ProtocolTypeDef.h>

#include <deque>
#include <optional>
#include <unordered_map>
#include <vector>

namespace bcos::engine
{

class PayloadCache
{
public:
    struct PutResult
    {
        std::vector<PayloadID> evicted;
    };

    PutResult put(PayloadID id, h256 const& blockHash, BuiltPayloadPtr entry);
    /// Atomically replace the cache with a single retained entry (put then retainOnly on a
    /// staging copy, then one noexcept swap). Used by newPayload commit so no intermediate
    /// multi-entry state is observable if retainOnly would fail after put.
    /// `evicted` lists every dropped id: FIFO overflow from the put *and* ids
    /// discarded by the retain step (callers that erase artifacts from `evicted`
    /// would otherwise leak the retain-dropped rows).
    PutResult putAndRetainOnly(PayloadID id, h256 const& blockHash, BuiltPayloadPtr entry);
    BuiltPayloadPtr find(const PayloadID& id) const;
    std::optional<PayloadID> payloadIdForHash(const h256& blockHash) const;
    std::optional<bcos::protocol::BlockNumber> blockNumberForHash(const h256& blockHash) const;
    void retainOnly(const PayloadID& id, const h256& blockHash);
    /// Remove one payload id from entries, hashToId, and FIFO order. Exclusive-lock
    /// callers only (same as put). CoW: a throw leaves the live cache unchanged.
    void erase(PayloadID const& id);
    /// Deep copy of the cache (for transactional rollback).
    PayloadCache duplicate() const;
    /// Replace live cache state from a snapshot (noexcept). Used to roll back failed publishes.
    void publishFrom(PayloadCache staged) noexcept;

private:
    static constexpr std::size_t c_maxEntries = 64;
    std::unordered_map<PayloadID, BuiltPayloadPtr> m_entries;
    std::unordered_map<h256, PayloadID> m_hashToId;
    std::deque<PayloadID> m_order;
};

}  // namespace bcos::engine
