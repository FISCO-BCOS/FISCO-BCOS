/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @brief the configuration of campaign leader
 * @file CampaignConfig.h
 * @author: yujiechen
 * @date 2022-04-26
 */
#pragma once
#include "ElectionConfig.h"

namespace bcos
{
namespace election
{
class CampaignConfig : public ElectionConfig
{
public:
    using Ptr = std::shared_ptr<CampaignConfig>;
    CampaignConfig(bcos::protocol::MemberInterface::Ptr _self, std::string const& _etcdEndPoint,
        bcos::protocol::MemberFactoryInterface::Ptr _memberFactory, std::string const& _leaderKey,
        std::string const& _purpose, unsigned _leaseTTL = 3)
      : ElectionConfig(_etcdEndPoint, _memberFactory, _purpose)
    {
        m_leaderKey = _leaderKey;
        m_leaseTTL = _leaseTTL;
        m_self = _self;
        m_self->encode(m_leaderValue);
        ELECTION_LOG(INFO) << LOG_DESC("CampaignConfig") << LOG_KV("selfID", m_self->memberID())
                           << LOG_KV("key", m_leaderKey) << LOG_KV("leaderValue", m_leaderValue);
    }

    ~CampaignConfig() override {}

    std::string const& leaderKey() const { return m_leaderKey; }
    virtual bcos::protocol::MemberInterface::Ptr getLeader();

    std::string const& leaderValue() const { return m_leaderValue; }
    unsigned leaseTTL() const { return m_leaseTTL; }
    bcos::protocol::MemberInterface::Ptr self() { return m_self; }

    void registerTriggerCampaignHandler(std::function<bool()> _triggerCampaign)
    {
        m_triggerCampaign = _triggerCampaign;
    }

protected:
    virtual void fetchLeaderInfoFromEtcd();
    virtual void onLeaderKeyChanged(etcd::Response _response);
    bool checkAndUpdateLeaderKey(etcd::Response _response);
    void reCreateWatcher() override
    {
        m_watcher = std::make_shared<etcd::Watcher>(*m_etcdClient, m_leaderKey,
            boost::bind(&CampaignConfig::onLeaderKeyChanged, this, boost::placeholders::_1));
    }

protected:
    // the leader key that multiple workers compete for, eg: "/consensus"
    std::string m_leaderKey;
    // default lease ttl is 3 seconds
    unsigned m_leaseTTL = 5;

    // the campaign leader info, eg: the grpc/tars endpoint address
    bcos::protocol::MemberInterface::Ptr m_self;
    std::string m_leaderValue;

    bcos::protocol::MemberInterface::Ptr m_leader;
    mutable bcos::SharedMutex x_leader;

    std::function<bool()> m_triggerCampaign;
};
}  // namespace election
}  // namespace bcos