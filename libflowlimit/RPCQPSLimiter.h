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
 * @file: QPSLimiter.cpp
 * @author: yujiechen
 * @date: 2020-04-15
 */
#pragma once
#include "QPSLimiter.h"
#include <libethcore/Protocol.h>

#define RPCQPSLIMIT_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("RPCQPSLimiter")

namespace dev
{
namespace flowlimit
{
class RPCQPSLimiter
{
public:
    using Ptr = std::shared_ptr<RPCQPSLimiter>;
    RPCQPSLimiter();
    virtual ~RPCQPSLimiter() {}
    virtual void createRPCQPSLimiter(uint64_t const& _maxQPS);
    void registerQPSLimiterByGroupID(dev::GROUP_ID const& _groupId, QPSLimiter::Ptr);

    QPSLimiter::Ptr getQPSLimiterByGroupId(dev::GROUP_ID const& _groupId);

    virtual bool acquire(int64_t const& _requiredPermits = 1);
    virtual bool acquireFromGroup(
        dev::GROUP_ID const& _groupId, int64_t const& _requiredPermits = 1);

    QPSLimiter::Ptr rpcQPSLimiter() { return m_rpcQPSLimiter; }

private:
    std::shared_ptr<std::map<dev::GROUP_ID, QPSLimiter::Ptr>> m_group2QPSLimiter;
    mutable SharedMutex x_group2QPSLimiter;
    QPSLimiter::Ptr m_rpcQPSLimiter;
};
}  // namespace flowlimit
}  // namespace dev