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
 * @brief : Implement of RPCQPSLimiter
 * @file: RPCQPSLimiter.cpp
 * @author: yujiechen
 * @date: 2020-04-20
 */

#include "RPCQPSLimiter.h"

using namespace dev::flowlimit;

RPCQPSLimiter::RPCQPSLimiter()
  : m_group2QPSLimiter(std::make_shared<std::map<dev::GROUP_ID, RateLimiter::Ptr>>())
{}

void RPCQPSLimiter::createRPCQPSLimiter(uint64_t const& _maxQPS)
{
    m_rpcQPSLimiter = std::make_shared<RateLimiter>(_maxQPS);
    RPCQPSLIMIT_LOG(INFO) << LOG_DESC("create RPCQPSLimiter") << LOG_KV("qps", _maxQPS);
}

void RPCQPSLimiter::registerQPSLimiterByGroupID(
    dev::GROUP_ID const& _groupId, RateLimiter::Ptr _qpsLimiter)
{
    WriteGuard l(x_group2QPSLimiter);
    if (m_group2QPSLimiter->count(_groupId))
    {
        RPCQPSLIMIT_LOG(INFO) << LOG_DESC("registerQPSLimiterByGroupID: already registered");
        return;
    }
    RPCQPSLIMIT_LOG(INFO) << LOG_DESC("registerQPSLimiterByGroupID") << LOG_KV("groupId", _groupId);
    (*m_group2QPSLimiter)[_groupId] = _qpsLimiter;
}

RateLimiter::Ptr RPCQPSLimiter::getQPSLimiterByGroupId(dev::GROUP_ID const& _groupId)
{
    ReadGuard l(x_group2QPSLimiter);
    if (!m_group2QPSLimiter->count(_groupId))
    {
        return nullptr;
    }
    return (*m_group2QPSLimiter)[_groupId];
}

bool RPCQPSLimiter::acquire(int64_t const& _requiredPermits)
{
    if (!m_rpcQPSLimiter)
    {
        return true;
    }
    return m_rpcQPSLimiter->tryAcquire(_requiredPermits);
}

bool RPCQPSLimiter::acquireFromGroup(dev::GROUP_ID const& _groupId, int64_t const& _requiredPermits)
{
    auto qpsLimiter = getQPSLimiterByGroupId(_groupId);
    if (!qpsLimiter)
    {
        return true;
    }
    return qpsLimiter->tryAcquire(_requiredPermits);
}

void RPCQPSLimiter::acquireWithoutWait(int64_t _requiredPermits)
{
    if (!m_rpcQPSLimiter)
    {
        return;
    }
    m_rpcQPSLimiter->acquireWithoutWait(_requiredPermits);
}