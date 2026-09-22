/**
 * @brief: response-callback correlation for pending with-response sends
 *
 * @file SessionCallback.h
 * @author: octopuswang
 * @date 2018-09-13
 */
#pragma once
#include "bcos-gateway/libnetwork/Common.h"
#include "bcos-gateway/libnetwork/FrameMeta.h"
#include <boost/asio/steady_timer.hpp>
#include <array>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace bcos::gateway
{

// The response frame, or nullopt when the request failed / timed out / the session dropped
// before any response arrived.
using SessionCallbackFunc = std::function<void(NetworkException, std::optional<FrameMeta>)>;

// SessionT: the session type the request was registered through (BasicSession<DecoderT, ...>).
template <typename SessionT>
struct ResponseCallback : public std::enable_shared_from_this<ResponseCallback<SessionT>>
{
    using Ptr = std::shared_ptr<ResponseCallback>;

    uint64_t startTime;
    SessionCallbackFunc callback;
    std::optional<boost::asio::steady_timer> timeoutHandler;
    // the session the request was registered through: the manager is shared host-wide, so a
    // routed response can be claimed on a different session than the owner — the owner's
    // pending-seq bookkeeping must be updated through this pointer, not the claiming session
    std::weak_ptr<SessionT> owner;
};

template <typename SessionT>
using SessionResponseCallback = ResponseCallback<SessionT>;

// Host-wide manager of pending response callbacks. One instance is owned by the Host and
// shared by every session of that host (a routed response can be claimed on a different
// session than the request went out on), so lookups are sharded into buckets to reduce lock
// contention.
template <typename SessionT>
class SessionCallbackManager
{
public:
    SessionCallbackManager() = default;
    SessionCallbackManager(const SessionCallbackManager&) = delete;
    SessionCallbackManager& operator=(const SessionCallbackManager&) = delete;

    typename SessionResponseCallback<SessionT>::Ptr getCallback(uint32_t seq, bool isRemove)
    {
        auto& bucket = m_buckets.at(seq % BucketNum);
        std::lock_guard<std::mutex> lockGuard(bucket.mutex);
        auto it = bucket.callbacks.find(seq);
        if (it == bucket.callbacks.end())
        {
            return nullptr;
        }

        auto callback = it->second;
        if (isRemove)
        {
            bucket.callbacks.erase(it);
        }

        return callback;
    }

    bool addCallback(uint32_t seq, typename SessionResponseCallback<SessionT>::Ptr callback)
    {
        auto& bucket = m_buckets.at(seq % BucketNum);
        std::lock_guard<std::mutex> lockGuard(bucket.mutex);
        return bucket.callbacks.try_emplace(seq, std::move(callback)).second;
    }

    bool removeCallback(uint32_t seq)
    {
        auto& bucket = m_buckets.at(seq % BucketNum);
        std::lock_guard<std::mutex> lockGuard(bucket.mutex);
        return bucket.callbacks.erase(seq) > 0;
    }

private:
    struct Bucket
    {
        std::mutex mutex;
        std::unordered_map<uint32_t, typename SessionResponseCallback<SessionT>::Ptr> callbacks;
    };

    static constexpr uint32_t BucketNum = 64;
    std::array<Bucket, BucketNum> m_buckets;
};

}  // namespace bcos::gateway
