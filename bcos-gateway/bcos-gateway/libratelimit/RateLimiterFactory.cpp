#include "RateLimiterFactory.h"

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