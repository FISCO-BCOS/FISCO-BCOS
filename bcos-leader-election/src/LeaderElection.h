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
 * @brief leader-election
 * @file LeaderElection.h
 * @author: yujiechen
 * @date 2022-04-26
 */
#pragma once
#include "CampaignConfig.h"
#include <bcos-framework/interfaces/election/LeaderElectionInterface.h>
#include <bcos-utilities/Timer.h>
#include <memory>
namespace bcos
{
namespace election
{
class LeaderElection : public LeaderElectionInterface,
                       public std::enable_shared_from_this<LeaderElection>
{
public:
    using Ptr = std::shared_ptr<LeaderElection>;
    LeaderElection(CampaignConfig::Ptr _config)
      : m_config(_config), m_etcdClient(_config->etcdClient())
    {
        m_config->registerTriggerCampaignHandler(
            boost::bind(&LeaderElection::campaignLeader, this));
        m_campaignTimer = std::make_shared<Timer>(m_config->leaseTTL() * 1000);
    }
    ~LeaderElection() override { stop(); }
    void start() override;

    void stop() override;
    void updateSelfConfig(bcos::protocol::MemberInterface::Ptr _self) override;
    bool electionClusterOk() const override { return m_config->electionClusterOk(); }

    // called when campaign success, this logic should start to work when campaign success
    void registerOnCampaignHandler(
        std::function<void(bool, bcos::protocol::MemberInterface::Ptr)> _onCampaignHandler) override
    {
        m_onCampaignHandler = _onCampaignHandler;
    }

    // called when keep-alive exception
    void registerKeepAliveExceptionHandler(
        std::function<void(std::exception_ptr)> _handler) override
    {
        m_onKeepAliveException = _handler;
    }

    // handler called when the election cluster down
    void registerOnElectionClusterException(std::function<void()> _handler) override
    {
        m_config->registerOnElectionClusterException(_handler);
    }
    // handler called when the election cluster recover
    void registerOnElectionClusterRecover(std::function<void()> _handler) override
    {
        m_config->registerOnElectionClusterRecover(_handler);
    }

protected:
    // campaign leader
    virtual bool campaignLeader();
    // grant lease with given ttl
    std::pair<bool, int64_t> grantLease();
    virtual void onKeepAliveException(std::exception_ptr _exception);
    virtual void tryToSwitchToBackup();

protected:
    CampaignConfig::Ptr m_config;
    std::shared_ptr<etcd::Client> m_etcdClient;
    std::shared_ptr<etcd::KeepAlive> m_keepAlive;
    std::function<void(std::exception_ptr)> m_onKeepAliveException;
    std::function<void(bool, bcos::protocol::MemberInterface::Ptr)> m_onCampaignHandler;
    mutable RecursiveMutex m_mutex;

    // for trigger campaign after disconnect
    std::shared_ptr<Timer> m_campaignTimer;
};
}  // namespace election
}  // namespace bcos