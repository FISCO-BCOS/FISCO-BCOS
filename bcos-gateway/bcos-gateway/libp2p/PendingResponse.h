/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file PendingResponse.h
 * @brief Node-level pending-request table: request/response correlation is libp2p policy
 *        (libnetwork is a pure frame transport — it neither allocates seqs nor matches
 *        responses). One table per Service, keyed by the seq the request carried; a routed
 *        response can arrive on ANY session of this node, so the table's scope is the whole
 *        node, never a single session.
 */
#pragma once

#include "bcos-network/Common.h"
#include "bcos-network/FrameMeta.h"
#include "bcos-gateway/libp2p/P2PDecoder.h"
#include <boost/asio/steady_timer.hpp>
#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace bcos::gateway
{

// One in-flight with-response request. The completion callback settles the sending coroutine's
// result slot (see P2PSession::fastSendP2PMessage); it is invoked exactly once because every
// settlement path — ack, timeout, disconnect flush, stop flush, write-failure reclaim — goes
// through PendingResponseTable::claim (atomic erase).
struct PendingResponse
{
    using Ptr = std::shared_ptr<PendingResponse>;

    std::function<void(bcos::network::NetworkException, std::optional<bcos::network::FrameMeta>)> callback;
    std::optional<boost::asio::steady_timer> timeoutHandler;
    // The session the request went OUT on. The matching response may arrive on a different
    // session (routed), but the request fails when its outbound session dies — the disconnect
    // flush claims entries by this owner.
    std::weak_ptr<Session> owner;
};

// Node-level seq -> pending-request map, sharded to bound lock contention (requests register
// and responses claim from every session thread). Owned by Service.
class PendingResponseTable
{
public:
    PendingResponseTable() = default;
    PendingResponseTable(const PendingResponseTable&) = delete;
    PendingResponseTable& operator=(const PendingResponseTable&) = delete;

    // Register a pending request. Returns false when the seq is already taken — with the
    // Service-wide monotonic seq allocator this cannot happen; a false return means a bug, so
    // the caller must fail the send instead of waiting on a response that can never be claimed
    // to this entry.
    bool add(uint32_t seq, PendingResponse::Ptr pending)
    {
        auto& bucket = m_buckets.at(seq % BucketNum);
        std::lock_guard<std::mutex> lockGuard(bucket.mutex);
        return bucket.pending.try_emplace(seq, std::move(pending)).second;
    }

    // Atomically erase and return the entry for seq (nullptr when already settled). This is the
    // exactly-once primitive every settlement path funnels through.
    PendingResponse::Ptr claim(uint32_t seq)
    {
        auto& bucket = m_buckets.at(seq % BucketNum);
        std::lock_guard<std::mutex> lockGuard(bucket.mutex);
        auto it = bucket.pending.find(seq);
        if (it == bucket.pending.end())
        {
            return nullptr;
        }
        auto pending = std::move(it->second);
        bucket.pending.erase(it);
        return pending;
    }

    // Erase and return every entry owned by the given session (the disconnect flush), plus
    // entries whose owner session is already gone (defensive: their flush was missed).
    std::vector<PendingResponse::Ptr> claimAllOf(Session const* owner)
    {
        std::vector<PendingResponse::Ptr> claimed;
        for (auto& bucket : m_buckets)
        {
            std::lock_guard<std::mutex> lockGuard(bucket.mutex);
            for (auto it = bucket.pending.begin(); it != bucket.pending.end();)
            {
                auto ownerSession = it->second->owner.lock();
                if (!ownerSession || ownerSession.get() == owner)
                {
                    claimed.emplace_back(std::move(it->second));
                    it = bucket.pending.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
        return claimed;
    }

    // Erase and return every entry (Service::stop: no waiter may outlive the service).
    std::vector<PendingResponse::Ptr> claimAll()
    {
        std::vector<PendingResponse::Ptr> claimed;
        for (auto& bucket : m_buckets)
        {
            std::lock_guard<std::mutex> lockGuard(bucket.mutex);
            for (auto& [seq, pending] : bucket.pending)
            {
                claimed.emplace_back(std::move(pending));
            }
            bucket.pending.clear();
        }
        return claimed;
    }

    // Test/diagnostic support.
    bool contains(uint32_t seq) const
    {
        auto& bucket = m_buckets.at(seq % BucketNum);
        std::lock_guard<std::mutex> lockGuard(bucket.mutex);
        return bucket.pending.find(seq) != bucket.pending.end();
    }

private:
    struct Bucket
    {
        mutable std::mutex mutex;
        std::unordered_map<uint32_t, PendingResponse::Ptr> pending;
    };

    static constexpr uint32_t BucketNum = 64;
    std::array<Bucket, BucketNum> m_buckets;
};

}  // namespace bcos::gateway
