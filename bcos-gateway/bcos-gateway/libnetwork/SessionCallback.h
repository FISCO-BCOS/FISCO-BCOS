/**
 * @brief: inteface for boost::asio(for unittest)
 *
 * @file CallbackInterface.h
 * @author: octopuswang
 * @date 2018-09-13
 */
#pragma once
#include "bcos-gateway/libnetwork/Common.h"
#include "bcos-gateway/libnetwork/Message.h"
#include <boost/asio/steady_timer.hpp>
#include <array>
#include <iterator>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace bcos::gateway
{

using SessionCallbackFunc = std::function<void(NetworkException, Message::Ptr)>;

struct ResponseCallback : public std::enable_shared_from_this<ResponseCallback>
{
    using Ptr = std::shared_ptr<ResponseCallback>;

    uint64_t startTime;
    SessionCallbackFunc callback;
    std::optional<boost::asio::steady_timer> timeoutHandler;
};

using SessionResponseCallback = ResponseCallback;

class SessionCallbackManagerInterface
{
public:
    using Ptr = std::shared_ptr<SessionCallbackManagerInterface>;
    using ConstPtr = std::shared_ptr<const SessionCallbackManagerInterface>;

    SessionCallbackManagerInterface() = default;
    SessionCallbackManagerInterface(const SessionCallbackManagerInterface&) = default;
    SessionCallbackManagerInterface(SessionCallbackManagerInterface&&) = default;
    SessionCallbackManagerInterface& operator=(const SessionCallbackManagerInterface&);
    SessionCallbackManagerInterface& operator=(SessionCallbackManagerInterface&&) noexcept;

    virtual ~SessionCallbackManagerInterface() = default;

    virtual SessionResponseCallback::Ptr getCallback(uint32_t seq, bool isRemove) = 0;
    virtual bool addCallback(uint32_t seq, SessionResponseCallback::Ptr callback) = 0;
    virtual bool removeCallback(uint32_t seq) = 0;
    // drain all pending callbacks (used by Session::drop to fail response waiters on teardown)
    virtual std::vector<SessionResponseCallback::Ptr> popAllCallbacks() = 0;
};

class SessionCallbackManager : public SessionCallbackManagerInterface
{
public:
    using Ptr = std::shared_ptr<SessionCallbackManager>;
    using ConstPtr = std::shared_ptr<const SessionCallbackManager>;

    SessionCallbackManager() = default;
    SessionCallbackManager(const SessionCallbackManager&) = delete;
    SessionCallbackManager(SessionCallbackManager&&) = delete;
    SessionCallbackManager& operator=(const SessionCallbackManager&) = delete;
    SessionCallbackManager& operator=(SessionCallbackManager&&) noexcept = delete;

    ~SessionCallbackManager() override = default;

    SessionResponseCallback::Ptr getCallback(uint32_t seq, bool isRemove) override
    {
        std::lock_guard<std::mutex> lockGuard(x_sessionCallbackMap);
        auto it = m_sessionCallbackMap.find(seq);
        if (it == m_sessionCallbackMap.end())
        {
            return nullptr;
        }

        auto callback = it->second;
        if (isRemove)
        {
            m_sessionCallbackMap.erase(it);
        }

        return callback;
    }

    bool addCallback(uint32_t seq, SessionResponseCallback::Ptr callback) override
    {
        std::lock_guard<std::mutex> lockGuard(x_sessionCallbackMap);
        auto result = m_sessionCallbackMap.try_emplace(seq, std::move(callback));
        return result.second;
    }

    bool removeCallback(uint32_t seq) override
    {
        std::lock_guard<std::mutex> lockGuard(x_sessionCallbackMap);
        auto result = m_sessionCallbackMap.erase(seq);
        return result > 0;
    }

    std::vector<SessionResponseCallback::Ptr> popAllCallbacks() override
    {
        std::lock_guard<std::mutex> lockGuard(x_sessionCallbackMap);
        std::vector<SessionResponseCallback::Ptr> callbacks;
        callbacks.reserve(m_sessionCallbackMap.size());
        for (auto& [seq, callback] : m_sessionCallbackMap)
        {
            callbacks.emplace_back(std::move(callback));
        }
        m_sessionCallbackMap.clear();
        return callbacks;
    }

private:
    std::mutex x_sessionCallbackMap;
    std::unordered_map<uint32_t, SessionResponseCallback::Ptr> m_sessionCallbackMap;
};

class SessionCallbackManagerBucket : public SessionCallbackManagerInterface
{
public:
    using Ptr = std::shared_ptr<SessionCallbackManagerBucket>;
    using ConstPtr = std::shared_ptr<const SessionCallbackManagerBucket>;

    SessionCallbackManagerBucket() = default;
    SessionCallbackManagerBucket(const SessionCallbackManagerBucket&) = delete;
    SessionCallbackManagerBucket(SessionCallbackManagerBucket&&) = delete;
    SessionCallbackManagerBucket& operator=(const SessionCallbackManagerBucket&) = delete;
    SessionCallbackManagerBucket& operator=(SessionCallbackManagerBucket&&) noexcept = delete;

    ~SessionCallbackManagerBucket() override = default;

    SessionResponseCallback::Ptr getCallback(uint32_t seq, bool isRemove) override
    {
        auto bucket = (seq % SessionCallbackBucketNum);
        return m_sessionCallbackBucket.at(bucket).getCallback(seq, isRemove);
    }

    bool addCallback(uint32_t seq, SessionResponseCallback::Ptr callback) override
    {
        auto bucket = (seq % SessionCallbackBucketNum);
        return m_sessionCallbackBucket.at(bucket).addCallback(seq, callback);
    }

    bool removeCallback(uint32_t seq) override
    {
        auto bucket = (seq % SessionCallbackBucketNum);
        return m_sessionCallbackBucket.at(bucket).removeCallback(seq);
    }

    std::vector<SessionResponseCallback::Ptr> popAllCallbacks() override
    {
        std::vector<SessionResponseCallback::Ptr> callbacks;
        for (auto& bucket : m_sessionCallbackBucket)
        {
            auto bucketCallbacks = bucket.popAllCallbacks();
            callbacks.insert(callbacks.end(),
                std::make_move_iterator(bucketCallbacks.begin()),
                std::make_move_iterator(bucketCallbacks.end()));
        }
        return callbacks;
    }

private:
    static constexpr uint32_t SessionCallbackBucketNum = 64;
    std::array<SessionCallbackManager, SessionCallbackBucketNum> m_sessionCallbackBucket;
};

}  // namespace bcos::gateway