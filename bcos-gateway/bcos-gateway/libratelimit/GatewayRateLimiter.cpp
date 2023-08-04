/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @file GatewayRateLimiter.cpp
 * @author: octopus
 * @date 2022-09-30
 */

#include "bcos-utilities/BoostLog.h"
#include <bcos-gateway/libratelimit/GatewayRateLimiter.h>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::gateway::ratelimiter;

std::optional<std::string> GatewayRateLimiter::checkOutGoing(const std::string& _endpoint,
    uint16_t _pkgType, const std::string& _groupID, uint16_t _moduleID, int64_t _msgLength)
{
    // endpoint of the p2p connection
    const std::string& endpoint = _endpoint;
    // group of the message, empty string means the message is p2p's own message
    const std::string& groupID = _groupID;
    // moduleID of the message, zero means the message is p2p's own message
    uint16_t moduleID = _moduleID;
    // the length of the message
    int64_t msgLength = _msgLength;

    std::string errorMsg;

    do
    {
        // total outgoing bandwidth
        bcos::ratelimiter::RateLimiterInterface::Ptr totalOutGoingBWLimit =
            m_rateLimiterManager->getRateLimiter(
                ratelimiter::RateLimiterManager::TOTAL_OUTGOING_KEY);

        // connection outgoing bandwidth
        bcos::ratelimiter::RateLimiterInterface::Ptr connOutGoingBWLimit =
            m_rateLimiterManager->getConnRateLimiter(endpoint);

        // group outgoing bandwidth
        bcos::ratelimiter::RateLimiterInterface::Ptr groupOutGoingBWLimit = nullptr;
        if (!groupID.empty())
        {
            groupOutGoingBWLimit = m_rateLimiterManager->getGroupRateLimiter(groupID);
        }

        const auto& modulesWithoutLimit = m_rateLimiterManager->modulesWithoutLimit();

        // if moduleID is zero, the P2P network itself's message, the ratelimiter does not limit
        // P2P own's messages
        if (moduleID == 0)
        {
            if (totalOutGoingBWLimit)
            {
                totalOutGoingBWLimit->tryAcquire(msgLength);
            }

            if (connOutGoingBWLimit)
            {
                connOutGoingBWLimit->tryAcquire(msgLength);
            }
        }
        // if moduleID is not zero, the message comes from the front
        // There are two scenarios:
        //  1. ulimit module message rate or
        //  2. limit module message rate
        else if (modulesWithoutLimit.at(moduleID))
        {  // case 1: ulimit module message rate or, just for statistic

            if (totalOutGoingBWLimit)
            {
                totalOutGoingBWLimit->tryAcquire(msgLength);
            }

            if (connOutGoingBWLimit)
            {
                connOutGoingBWLimit->tryAcquire(msgLength);
            }

            if (groupOutGoingBWLimit)
            {
                groupOutGoingBWLimit->tryAcquire(msgLength);
            }
        }
        else
        {  // case 2: limit module message rate

            if (totalOutGoingBWLimit && !totalOutGoingBWLimit->tryAcquire(msgLength))
            {
                // total outgoing bandwidth overflow
                errorMsg = "the network total outgoing bandwidth overflow";
                break;
            }

            if (connOutGoingBWLimit && !connOutGoingBWLimit->tryAcquire(msgLength))
            {
                // connection outgoing bandwidth overflow
                errorMsg =
                    "the network connection outgoing bandwidth overflow, endpoint: " + endpoint;
                if (totalOutGoingBWLimit)
                {
                    totalOutGoingBWLimit->rollback(msgLength);
                }

                break;
            }

            if (groupOutGoingBWLimit && !groupOutGoingBWLimit->tryAcquire(msgLength))
            {
                // group outgoing bandwidth overflow
                errorMsg = "the group outgoing bandwidth overflow, groupID: " + groupID;
                if (totalOutGoingBWLimit)
                {
                    totalOutGoingBWLimit->rollback(msgLength);
                }

                if (connOutGoingBWLimit)
                {
                    connOutGoingBWLimit->rollback(msgLength);
                }

                break;
            }
        }

        m_rateLimiterStat->updateOutGoing(endpoint, msgLength, true);
        m_rateLimiterStat->updateOutGoing(groupID, moduleID, msgLength, true);

        return std::nullopt;
    } while (false);

    GATEWAY_LOG(TRACE) << LOG_BADGE("checkOutGoing") << LOG_DESC("outgoing bandwidth overflow")
                       << LOG_KV("endpoint", _endpoint) << LOG_KV("pkgType", _pkgType)
                       << LOG_KV("groupID", groupID) << LOG_KV("moduleID", moduleID);

    m_rateLimiterStat->updateOutGoing(endpoint, msgLength, false);
    m_rateLimiterStat->updateOutGoing(groupID, moduleID, msgLength, false);

    return {errorMsg};
}

std::optional<std::string> GatewayRateLimiter::checkInComing(
    const std::string& _endpoint, uint16_t _packageType, int64_t _msgLength, bool /*unused*/)
{
    auto rateLimiter = m_rateLimiterManager->getInRateLimiter(_endpoint, _packageType);
    bool result = true;
    if (rateLimiter)
    {
        result = rateLimiter->tryAcquire(1);
        if (!result)
        {
            GATEWAY_LOG(TRACE) << LOG_BADGE("checkInComing") << LOG_DESC("incoming qps overflow")
                               << LOG_KV("endpoint", _endpoint)
                               << LOG_KV("packageType", _packageType)
                               << LOG_KV("msgLength", _msgLength);
        }
    }
    m_rateLimiterStat->updateInComing0(_endpoint, _packageType, _msgLength, result);
    return result ? std::nullopt :
                    std::make_optional<std::string>(
                        "incoming qps overflow, package type: " + std::to_string(_packageType) +
                        " ,endpoint: " + _endpoint);
}

std::optional<std::string> GatewayRateLimiter::checkInComing(
    const std::string& _groupID, uint16_t _moduleID, int64_t _msgLength)
{
    bool result = true;
    auto rateLimiter = m_rateLimiterManager->getInRateLimiter(_groupID, _moduleID, true);
    if (rateLimiter)
    {
        result = rateLimiter->tryAcquire(1);
        if (!result)
        {
            GATEWAY_LOG(TRACE) << LOG_BADGE("checkInComing") << LOG_DESC("incoming qps overflow")
                               << LOG_KV("groupID", _groupID) << LOG_KV("moduleID", _moduleID)
                               << LOG_KV("msgLength", _msgLength);
        }
    }
    m_rateLimiterStat->updateInComing(_groupID, _moduleID, _msgLength, result);
    return result ? std::nullopt :
                    std::make_optional<std::string>("incoming qps overflow, groupID: " + _groupID +
                                                    " ,moduleID: " + std::to_string(_moduleID));
}
