/*
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
 */
/**
 * @brief : Implement of QPSLimiter
 * @file: QPSLimiter.h
 * @author: yujiechen
 * @date: 2020-04-15
 */
#pragma once
#include <libdevcore/Common.h>
#include <libdevcore/Guards.h>

#define QPSLIMIT_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("QPSLimiter")

namespace dev
{
namespace limit
{
class QPSLimiter
{
public:
    using Ptr = std::shared_ptr<QPSLimiter>;
    QPSLimiter(uint64_t const& _maxQPS);
    // acquire permits
    void acquire(int64_t const& _requiredPermits = 1);

protected:
    int64_t fetchPermitsAndGetWaitTime(int64_t const& _requiredPermits);
    void updatePermits(int64_t const& _now);

private:
    mutable dev::Mutex m_mutex;

    // the max QPS
    uint64_t m_maxQPS;

    // stored permits
    int64_t m_currentStoredPermits = 0;

    // the interval time to update storedPermits
    double m_permitsUpdateInterval;
    int64_t m_lastPermitsUpdateTime;
};
}  // namespace limit
}  // namespace dev