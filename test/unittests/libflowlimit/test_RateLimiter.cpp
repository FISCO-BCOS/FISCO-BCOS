/**
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2020 fisco-dev contributors.
 *
 * @brief: unit test for RateLimiter
 *
 * @file test_RateLimiter.cpp
 * @author: yujiechen
 * @date 20200506
 */
#include <libflowlimit/RateLimiter.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <thread>

using namespace bcos::flowlimit;
namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(RateLimiterTest, TestOutputHelperFixture)

class RateLimiterTest : public RateLimiter
{
public:
    RateLimiterTest(uint64_t const& _maxQPS) : RateLimiter(_maxQPS) {}
    virtual ~RateLimiterTest() {}

    int64_t const& maxPermits() { return m_maxPermits; }

    double const& permitsUpdateInterval() { return m_permitsUpdateInterval; }

    int64_t const& maxBurstReqNum() { return m_maxBurstReqNum; }

    int64_t burstReqNum() { return m_burstReqNum; }

    int64_t const& currentStoredPermits() { return m_currentStoredPermits; }

    int64_t const& lastPermitsUpdateTime() { return m_lastPermitsUpdateTime; }

    int64_t futureBurstResetTime() const { return m_futureBurstResetTime; }

    void setFutureBurstResetTime(int64_t const& _futureBurstResetTime)
    {
        m_futureBurstResetTime = _futureBurstResetTime;
    }

    int64_t wrapperFetchPermitsAndGetWaitTime(int64_t const& _requiredPermits,
        bool const& _fetchPermitsWhenRequireWait, int64_t const& _now)
    {
        return RateLimiter::fetchPermitsAndGetWaitTime(
            _requiredPermits, _fetchPermitsWhenRequireWait, _now);
    }
    void wrapperUpdatePermits(int64_t const& _now) { return RateLimiter::updatePermits(_now); }
};

BOOST_AUTO_TEST_CASE(testTryAcquireWithoutBurst)
{
    int64_t maxQPS = 2000;
    auto rateLimiter = std::make_shared<RateLimiterTest>(maxQPS);
    BOOST_CHECK(rateLimiter->maxPermits() == 2000);
    BOOST_CHECK(rateLimiter->permitsUpdateInterval() == 500);
    BOOST_CHECK(rateLimiter->currentStoredPermits() == 0);
    int64_t sleepInterval = 1000000 / maxQPS;

    // with enough permits
    for (int64_t i = 0; i < maxQPS; i++)
    {
        std::this_thread::sleep_for(std::chrono::microseconds(sleepInterval));
        BOOST_CHECK(rateLimiter->tryAcquire() == true);
    }
}

BOOST_AUTO_TEST_CASE(testFetchPermitsAndGetWaitTime)
{
    int64_t qpsLimit = 50000;
    int64_t intervalPerPermit = 1000000 / qpsLimit;
    auto rateLimiter = std::make_shared<RateLimiterTest>(qpsLimit);
    BOOST_CHECK(rateLimiter->maxPermits() == qpsLimit);
    BOOST_CHECK(rateLimiter->permitsUpdateInterval() == intervalPerPermit);
    BOOST_CHECK(rateLimiter->currentStoredPermits() == 0);

    /// case1: insufficient permits
    int64_t orglastPermitsUpdateTime = rateLimiter->lastPermitsUpdateTime();
    // set now to be smaller than m_lastPermitsUpdateTime
    auto now = rateLimiter->lastPermitsUpdateTime() - 10;
    // acquire permits failed, return wait time large than zero directly
    BOOST_CHECK(rateLimiter->tryAcquire(1, now) == false);
    BOOST_CHECK(rateLimiter->lastPermitsUpdateTime() == orglastPermitsUpdateTime);

    // acquire permits failed, return the wait time that can acquire the permit
    BOOST_CHECK(rateLimiter->wrapperFetchPermitsAndGetWaitTime(1, true, now) == 10);
    BOOST_CHECK(
        rateLimiter->lastPermitsUpdateTime() == orglastPermitsUpdateTime + intervalPerPermit);

    // Because there is not enough time, get 0 permits, advance the future permits
    now = rateLimiter->lastPermitsUpdateTime() + 10;
    BOOST_CHECK(rateLimiter->tryAcquire(1, now) == true);
    BOOST_CHECK(rateLimiter->currentStoredPermits() == 0);
    BOOST_CHECK(rateLimiter->lastPermitsUpdateTime() == now + 10);

    // won't update lastePermitsUpdateTime for the request will not wait to occupy a permit
    BOOST_CHECK(rateLimiter->tryAcquire(1, now) == false);
    BOOST_CHECK(rateLimiter->lastPermitsUpdateTime() == now + 10);

    // will update lastePermitsUpdateTime for the request will wait to occupy a permit
    BOOST_CHECK(rateLimiter->wrapperFetchPermitsAndGetWaitTime(1, true, now) == 10);
    BOOST_CHECK(rateLimiter->lastPermitsUpdateTime() == now + 10 + intervalPerPermit);

    /// case2: enough permits
    int64_t reqNum = 1000;
    now = rateLimiter->lastPermitsUpdateTime() + 2 * reqNum * intervalPerPermit;
    // Consume half of the permits
    BOOST_CHECK(rateLimiter->tryAcquire(reqNum, now) == true);
    BOOST_CHECK(rateLimiter->lastPermitsUpdateTime() == now);
    BOOST_CHECK(rateLimiter->currentStoredPermits() == reqNum);
    BOOST_CHECK(rateLimiter->wrapperFetchPermitsAndGetWaitTime(reqNum, true, now) == 0);
    BOOST_CHECK(rateLimiter->lastPermitsUpdateTime() == now);
    BOOST_CHECK(rateLimiter->currentStoredPermits() == 0);
}

BOOST_AUTO_TEST_CASE(testAcquireWithBurstSupported)
{
    int64_t maxQPSLimit = 100000;
    int64_t intervalPerPermit = 1000000 / maxQPSLimit;
    auto rateLimiter = std::make_shared<RateLimiterTest>(maxQPSLimit);
    // set burst
    int64_t burstReqNum = 0.2 * maxQPSLimit;
    rateLimiter->setMaxBurstReqNum(burstReqNum);

    // set now to be smaller than m_lastPermitsUpdateTime
    auto now = rateLimiter->lastPermitsUpdateTime() - 100;
    // accept the burst request
    BOOST_CHECK(rateLimiter->acquireWithBurstSupported(10, now) == true);
    BOOST_CHECK(rateLimiter->burstReqNum() == 10);

    now = rateLimiter->lastPermitsUpdateTime() + intervalPerPermit * 20;
    // consume 40 permits in advance
    BOOST_CHECK(rateLimiter->acquireWithBurstSupported(40, now) == true);
    BOOST_CHECK(rateLimiter->lastPermitsUpdateTime() == now + intervalPerPermit * 20);

    // consume 40 burst permits
    BOOST_CHECK(rateLimiter->acquireWithBurstSupported(40, now) == true);
    BOOST_CHECK(rateLimiter->burstReqNum() == 50);
    BOOST_CHECK(rateLimiter->lastPermitsUpdateTime() == now + intervalPerPermit * 20);
    BOOST_CHECK(rateLimiter->currentStoredPermits() == 0);

    // another 1000 burst request
    BOOST_CHECK(rateLimiter->acquireWithBurstSupported(burstReqNum, now) == false);
    BOOST_CHECK(rateLimiter->burstReqNum() == 50);

    // require only 950 permits
    BOOST_CHECK(rateLimiter->acquireWithBurstSupported(burstReqNum - 50, now) == true);
    BOOST_CHECK(rateLimiter->burstReqNum() == burstReqNum);

    now = rateLimiter->lastPermitsUpdateTime() + intervalPerPermit * 20;
    BOOST_CHECK(rateLimiter->acquireWithBurstSupported(10, now) == true);
    BOOST_CHECK(rateLimiter->lastPermitsUpdateTime() == now);
    BOOST_CHECK(rateLimiter->currentStoredPermits() == 10);

    BOOST_CHECK(rateLimiter->acquireWithBurstSupported(10, now) == true);
    BOOST_CHECK(rateLimiter->currentStoredPermits() == 0);
    BOOST_CHECK(rateLimiter->lastPermitsUpdateTime() == now);

    // test reset m_futureBurstResetTime
    now = rateLimiter->futureBurstResetTime() + 1000001;
    auto orgFutureBurstResetTime = rateLimiter->futureBurstResetTime();
    int64_t releasedPermits = (now - rateLimiter->lastPermitsUpdateTime()) / intervalPerPermit;
    int64_t updatedLastTime = orgFutureBurstResetTime + intervalPerPermit * releasedPermits;
    if (releasedPermits > maxQPSLimit)
    {
        releasedPermits = maxQPSLimit;
        updatedLastTime = now;
    }
    // release new burst requests
    BOOST_CHECK(rateLimiter->acquireWithBurstSupported(1, now) == true);
    BOOST_CHECK(rateLimiter->futureBurstResetTime() == orgFutureBurstResetTime);

    BOOST_CHECK(rateLimiter->currentStoredPermits() == (releasedPermits - 1));

    BOOST_CHECK(rateLimiter->futureBurstResetTime() == orgFutureBurstResetTime);
    BOOST_CHECK(rateLimiter->lastPermitsUpdateTime() == updatedLastTime);

    BOOST_CHECK(rateLimiter->acquireWithBurstSupported(releasedPermits, now) == true);
    BOOST_CHECK(rateLimiter->lastPermitsUpdateTime() == updatedLastTime + intervalPerPermit);
    BOOST_CHECK(rateLimiter->currentStoredPermits() == 0);

    BOOST_CHECK(rateLimiter->acquireWithBurstSupported(burstReqNum, now) == true);
    BOOST_CHECK(rateLimiter->futureBurstResetTime() == now + 1000000);
    BOOST_CHECK(rateLimiter->burstReqNum() == burstReqNum);
    BOOST_CHECK(rateLimiter->lastPermitsUpdateTime() == updatedLastTime + intervalPerPermit);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
