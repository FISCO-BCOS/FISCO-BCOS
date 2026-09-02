/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "SplitEngineCommon.h"

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

    PutResult put(PayloadID id, h256 blockHash, CommonPayloadEntryPtr entry);
    /// Insert without FIFO eviction or order tracking. Matches the legacy generic FCU build
    /// path, which only grows the payload map until newPayload retainOnly clears it.
    PutResult putUnbounded(PayloadID id, h256 blockHash, CommonPayloadEntryPtr entry);
    /// Atomically replace the cache with a single retained entry (put then retainOnly on a
    /// staging copy, then one noexcept swap). Used by newPayload commit so no intermediate
    /// multi-entry state is observable if retainOnly would fail after put.
    PutResult putAndRetainOnly(PayloadID id, h256 blockHash, CommonPayloadEntryPtr entry);
    CommonPayloadEntryPtr find(const PayloadID& id) const;
    std::optional<PayloadID> payloadIdForHash(const h256& blockHash) const;
    std::optional<bcos::protocol::BlockNumber> blockNumberForHash(const h256& blockHash) const;
    void retainOnly(const PayloadID& id, const h256& blockHash);
    /// Deep copy of the cache (for transactional rollback).
    PayloadCache duplicate() const;
    /// Replace live cache state from a snapshot (noexcept). Used to roll back failed publishes.
    void publishFrom(PayloadCache staged) noexcept;

private:
    static constexpr std::size_t c_maxEntries = 64;
    std::unordered_map<PayloadID, CommonPayloadEntryPtr> m_entries;
    std::unordered_map<h256, PayloadID> m_hashToId;
    std::deque<PayloadID> m_order;
};

}  // namespace bcos::engine
