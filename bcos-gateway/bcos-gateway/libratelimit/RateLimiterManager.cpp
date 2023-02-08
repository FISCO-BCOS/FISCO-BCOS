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
 * @file RateLimiterManager.cpp
 * @author: octopus
 * @date 2022-06-30
 */

#include "bcos-gateway/Common.h"
#include "bcos-utilities/BoostLog.h"
#include <bcos-gateway/libratelimit/RateLimiterManager.h>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::gateway::ratelimiter;

const std::string RateLimiterManager::TOTAL_OUTGOING_KEY = "total-outgoing-key";

void RateLimiterManager::initP2pBasicMsgTypes()
{
    /**
   enum GatewayMessageType : int16_t
  {
      Heartbeat = 0x1,
      Handshake = 0x2,
      RequestNodeStatus = 0x3,
      ResponseNodeStatus = 0x4,
      PeerToPeerMessage = 0x5,  //
      BroadcastMessage = 0x6,   //
      AMOPMessageType = 0x7,    //
      WSMessageType = 0x8,      //
      SyncNodeSeq = 0x9,
      RouterTableSyncSeq = 0xa,
      RouterTableResponse = 0xb,
      RouterTableRequest = 0xc,
      ForwardMessage = 0xd,     //
  };
  */

    m_p2pBasicMsgTypes.fill(false);

    for (uint16_t type : {GatewayMessageType::Heartbeat, GatewayMessageType::Handshake,
             GatewayMessageType::RequestNodeStatus, GatewayMessageType::ResponseNodeStatus,
             GatewayMessageType::SyncNodeSeq, GatewayMessageType::RouterTableSyncSeq,
             GatewayMessageType::RouterTableResponse, GatewayMessageType::RouterTableRequest})
    {
        m_p2pBasicMsgTypes.at(type) = true;
    }
}

RateLimiterInterface::Ptr RateLimiterManager::getRateLimiter(const std::string& _rateLimiterKey)
{
    std::shared_lock lock(x_rateLimiters);
    auto it = m_rateLimiters.find(_rateLimiterKey);
    if (it != m_rateLimiters.end())
    {
        return it->second;
    }

    return nullptr;
}

RateLimiterInterface::Ptr RateLimiterManager::registerRateLimiter(
    const std::string& _rateLimiterKey, RateLimiterInterface::Ptr _rateLimiter)
{
    RateLimiterInterface::Ptr oldRateLimiter = nullptr;
    {
        std::unique_lock lock(x_rateLimiters);

        auto it = m_rateLimiters.find(_rateLimiterKey);
        if (it == m_rateLimiters.end())
        {
            m_rateLimiters[_rateLimiterKey] = std::move(_rateLimiter);
        }
        else
        {
            oldRateLimiter = it->second;
        }
    }

    if (oldRateLimiter)
    {
        RATELIMIT_MGR_LOG(INFO) << LOG_BADGE("registerRateLimiter")
                                << LOG_DESC("register ratelimiter success")
                                << LOG_KV("rateLimiterKey", _rateLimiterKey);
    }
    else
    {
        RATELIMIT_MGR_LOG(INFO) << LOG_BADGE("registerRateLimiter")
                                << LOG_DESC("register ratelimiter has been registered")
                                << LOG_KV("rateLimiterKey", _rateLimiterKey);
    }

    return oldRateLimiter;
}

bool RateLimiterManager::removeRateLimiter(const std::string& _rateLimiterKey)
{
    bool result = false;
    {
        std::unique_lock lock(x_rateLimiters);
        result = (m_rateLimiters.erase(_rateLimiterKey) > 0);
    }

    RATELIMIT_MGR_LOG(INFO) << LOG_BADGE("removeRateLimiter")
                            << LOG_KV("rateLimiterKey", _rateLimiterKey)
                            << LOG_KV("result", result);
    return result;
}

RateLimiterInterface::Ptr RateLimiterManager::getGroupRateLimiter(const std::string& _group)
{
    if (!m_enableOutGroupRateLimit)
    {
        return nullptr;
    }

    const std::string& rateLimiterKey = _group;

    auto rateLimiter = getRateLimiter(rateLimiterKey);
    if (rateLimiter != nullptr)
    {
        return rateLimiter;
    }

    // rete limiter not exist, create it
    int64_t groupOutgoingBwLimit = -1;
    int32_t timeWindowS = m_rateLimiterConfig.timeWindowSec;
    int32_t timeWindowMS = m_rateLimiterConfig.timeWindowSec * 1000;
    bool allowExceedMaxPermitSize = m_rateLimiterConfig.allowExceedMaxPermitSize;

    auto it = m_rateLimiterConfig.group2BwLimit.find(_group);
    if (it != m_rateLimiterConfig.group2BwLimit.end())
    {
        groupOutgoingBwLimit = it->second;
    }
    else if (m_rateLimiterConfig.groupOutgoingBwLimit > 0)
    {
        groupOutgoingBwLimit = m_rateLimiterConfig.groupOutgoingBwLimit;
    }

    if (groupOutgoingBwLimit > 0)
    {
        RATELIMIT_MGR_LOG(INFO) << LOG_BADGE("getGroupRateLimiter")
                                << LOG_DESC("group rate limiter not exist")
                                << LOG_KV("rateLimiterKey", rateLimiterKey)
                                << LOG_KV("groupOutgoingBwLimit", groupOutgoingBwLimit)
                                << LOG_KV("timeWindowS", timeWindowS)
                                << LOG_KV("enableDistributedRatelimit",
                                       m_rateLimiterConfig.enableDistributedRatelimit);

        if (m_rateLimiterConfig.enableDistributedRatelimit)
        {
            rateLimiter = m_rateLimiterFactory->buildDistributedRateLimiter(
                m_rateLimiterFactory->toTokenKey(_group), groupOutgoingBwLimit * timeWindowS,
                timeWindowS, allowExceedMaxPermitSize,
                m_rateLimiterConfig.enableDistributedRateLimitCache,
                m_rateLimiterConfig.distributedRateLimitCachePercent);
        }
        else
        {
            rateLimiter = m_rateLimiterFactory->buildTimeWindowRateLimiter(
                groupOutgoingBwLimit * timeWindowS, timeWindowMS, allowExceedMaxPermitSize);
        }

        auto oldRateLimiter = registerRateLimiter(_group, rateLimiter);
        if (oldRateLimiter)
        {
            rateLimiter = oldRateLimiter;
        }
    }

    return rateLimiter;
}

RateLimiterInterface::Ptr RateLimiterManager::getInRateLimiter(
    const std::string& _connIP, uint16_t _packageType)
{
    // not enable msg type limit
    if (!m_rateLimiterConfig.enableInP2pBasicMsgLimit())
    {
        return nullptr;
    }

    // not basic msg type
    if (!isP2pBasicMsgType(_packageType))
    {
        return nullptr;
    }

    const std::string& inKey = _connIP + "_" + std::to_string(_packageType);

    auto rateLimiter = getRateLimiter(inKey);
    if (rateLimiter != nullptr)
    {
        return rateLimiter;
    }

    int32_t timeWindowS = m_rateLimiterConfig.timeWindowSec;
    int32_t timeWindowMS = m_rateLimiterConfig.timeWindowSec * 1000;
    bool allowExceedMaxPermitSize = m_rateLimiterConfig.allowExceedMaxPermitSize;
    int64_t QPS = m_rateLimiterConfig.p2pBasicMsgQPS;

    rateLimiter = m_rateLimiterFactory->buildTimeWindowRateLimiter(
        QPS * timeWindowS, timeWindowMS, allowExceedMaxPermitSize);

    auto result = registerRateLimiter(inKey, rateLimiter);

    return result ? result : rateLimiter;
}

RateLimiterInterface::Ptr RateLimiterManager::getInRateLimiter(
    const std::string& _groupID, uint16_t _moduleID, bool /***/)
{
    /*
    enum ModuleID
    {
        PBFT = 1000,
        Raft = 1001,

        BlockSync = 2000,
        TxsSync = 2001,
        ConsTxsSync = 2002,

        AMOP = 3000,

        LIGHTNODE_GET_BLOCK = 4000,
        LIGHTNODE_GET_TRANSACTIONS = 4001,
        LIGHTNODE_GET_RECEIPTS = 4002,
        LIGHTNODE_GET_STATUS = 4003,
        LIGHTNODE_SEND_TRANSACTION = 4004,
        LIGHTNODE_CALL = 4005,
        LIGHTNODE_GET_ABI = 4006,
        LIGHTNODE_END = 4999,

        SYNC_PUSH_TRANSACTION = 5000,
        SYNC_GET_TRANSACTIONS = 5001,
        SYNC_END = 5999
    };
    */
    if (!m_rateLimiterConfig.enableInP2pModuleMsgLimit(_moduleID))
    {
        return nullptr;
    }

    const std::string& inKey = _groupID + "_" + std::to_string(_moduleID);

    auto rateLimiter = getRateLimiter(inKey);
    if (rateLimiter != nullptr)
    {
        return rateLimiter;
    }

    int32_t timeWindowS = m_rateLimiterConfig.timeWindowSec;
    int32_t timeWindowMS = m_rateLimiterConfig.timeWindowSec * 1000;
    bool allowExceedMaxPermitSize = m_rateLimiterConfig.allowExceedMaxPermitSize;
    int64_t QPS = m_rateLimiterConfig.p2pModuleMsgQPS;
    auto enableDistributedRatelimit = m_rateLimiterConfig.enableDistributedRatelimit;

    const auto& moduleMsg2QPSLimit = m_rateLimiterConfig.moduleMsg2QPSLimit;
    auto it = moduleMsg2QPSLimit.find(_moduleID);
    if (it != moduleMsg2QPSLimit.end() && it->second > 0)
    {
        QPS = it->second;
    }

    if (enableDistributedRatelimit)
    {
        rateLimiter = m_rateLimiterFactory->buildDistributedRateLimiter(
            m_rateLimiterFactory->toTokenKey(inKey), QPS * timeWindowS, timeWindowS,
            allowExceedMaxPermitSize, m_rateLimiterConfig.enableDistributedRateLimitCache,
            m_rateLimiterConfig.distributedRateLimitCachePercent);
    }
    else
    {
        rateLimiter = m_rateLimiterFactory->buildTimeWindowRateLimiter(
            QPS * timeWindowS, timeWindowMS, allowExceedMaxPermitSize);
    }

    auto result = registerRateLimiter(inKey, rateLimiter);

    return result ? result : rateLimiter;
}

RateLimiterInterface::Ptr RateLimiterManager::getConnRateLimiter(const std::string& _connIP)
{
    if (!m_enableOutConRateLimit)
    {
        return nullptr;
    }

    const std::string& rateLimiterKey = _connIP;

    auto rateLimiter = getRateLimiter(rateLimiterKey);
    if (rateLimiter != nullptr)
    {
        return rateLimiter;
    }

    int64_t connOutgoingBwLimit = -1;
    int32_t timeWindowS = m_rateLimiterConfig.timeWindowSec;
    int32_t timeWindowMS = m_rateLimiterConfig.timeWindowSec * 1000;
    bool allowExceedMaxPermitSize = m_rateLimiterConfig.allowExceedMaxPermitSize;

    auto it = m_rateLimiterConfig.ip2BwLimit.find(_connIP);
    if (it != m_rateLimiterConfig.ip2BwLimit.end())
    {
        connOutgoingBwLimit = it->second;
    }
    else if (m_rateLimiterConfig.connOutgoingBwLimit > 0)
    {
        connOutgoingBwLimit = m_rateLimiterConfig.connOutgoingBwLimit;
    }

    if (connOutgoingBwLimit > 0)
    {
        RATELIMIT_MGR_LOG(INFO) << LOG_BADGE("getConnRateLimiter")
                                << LOG_DESC("conn rate limiter not exist")
                                << LOG_KV("rateLimiterKey", rateLimiterKey)
                                << LOG_KV("connOutgoingBwLimit", connOutgoingBwLimit)
                                << LOG_KV("timeWindowMS", timeWindowMS);

        // create ratelimiter
        rateLimiter = m_rateLimiterFactory->buildTimeWindowRateLimiter(
            connOutgoingBwLimit * timeWindowS, timeWindowMS, allowExceedMaxPermitSize);

        registerRateLimiter(rateLimiterKey, rateLimiter);
    }

    return rateLimiter;
}