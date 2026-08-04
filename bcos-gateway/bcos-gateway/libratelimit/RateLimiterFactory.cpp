#include "RateLimiterFactory.h"

bcos::gateway::ratelimiter::RateLimiterFactory::RateLimiterFactory(
    std::shared_ptr<sw::redis::Redis> _redis)
  : m_redis(std::move(_redis))
{}

std::shared_ptr<sw::redis::Redis> bcos::gateway::ratelimiter::RateLimiterFactory::redis() const
{
    return m_redis;
}

std::string bcos::gateway::ratelimiter::RateLimiterFactory::toTokenKey(const std::string& _baseKey)
{
    return "FISCO-BCOS 3.0 Gateway RateLimiter: " + _baseKey;
}

bcos::ratelimiter::RateLimiterInterface::Ptr
bcos::gateway::ratelimiter::RateLimiterFactory::buildTimeWindowRateLimiter(
    int64_t _maxPermits, int32_t _timeWindowMS, bool _allowExceedMaxPermitSize)
{
    auto rateLimiter = std::make_shared<bcos::ratelimiter::TimeWindowRateLimiter>(
        _maxPermits, _timeWindowMS, _allowExceedMaxPermitSize);
    return rateLimiter;
}

bcos::ratelimiter::RateLimiterInterface::Ptr
bcos::gateway::ratelimiter::RateLimiterFactory::buildDistributedRateLimiter(
    const std::string& _distributedKey, int64_t _maxPermitsSize, int32_t _intervalSec,
    bool _allowExceedMaxPermitSize, bool _enableLocalCache, int32_t _localCachePercent)
{
    auto rateLimiter = std::make_shared<bcos::ratelimiter::DistributedRateLimiter>(m_redis,
        _distributedKey, _maxPermitsSize, _allowExceedMaxPermitSize, _intervalSec,
        _enableLocalCache, _localCachePercent);
    return rateLimiter;
}