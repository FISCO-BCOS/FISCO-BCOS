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
 * @file LeaderElection.cpp
 * @author: yujiechen
 * @date 2022-04-26
 */
#include "LeaderElection.h"
#include <etcd/KeepAlive.hpp>
#include <etcd/v3/Transaction.hpp>

using namespace bcos;
using namespace bcos::election;

std::pair<bool, int64_t> LeaderElection::grantLease()
{
    auto response = m_etcdClient->leasegrant(m_config->leaseTTL()).get();
    if (!response.is_ok())
    {
        ELECTION_LOG(ERROR) << LOG_DESC("grantLease error")
                            << LOG_KV("msg", response.error_message())
                            << LOG_KV("code", response.error_code());
        return std::make_pair(false, 0);
    }
    auto leaseID = response.value().lease();
    ELECTION_LOG(INFO) << LOG_DESC("grantLease success") << LOG_KV("id", leaseID)
                       << LOG_KV("ttl", response.value().ttl())
                       << LOG_KV("purpose", m_config->purpose());
    return std::make_pair(true, leaseID);
}

bool LeaderElection::campaignLeader()
{
    try
    {
        RecursiveGuard l(m_mutex);
        auto ret = grantLease();
        if (!ret.first)
        {
            tryToSwitchToBackup();
            m_campaignTimer->restart();
            return false;
        }
        auto leaseID = ret.second;
        auto tx = std::make_shared<etcdv3::Transaction>(m_config->leaderKey());
        tx->init_compare(0, etcdv3::CompareResult::EQUAL, etcdv3::CompareTarget::MOD);
        tx->setup_basic_failure_operation(m_config->leaderKey());
        tx->setup_basic_create_sequence(m_config->leaderKey(), m_config->leaderValue(), leaseID);
        auto response = m_etcdClient->txn(*tx).get();
        if (!response.is_ok())
        {
            // failed for non-compare-failed error, restart campaign
            if (response.error_code() != etcdv3::ERROR_COMPARE_FAILED)
            {
                m_campaignTimer->restart();
            }
            else
            {
                // failed for compare-failed error, stop campaign
                m_campaignTimer->stop();
            }
            ELECTION_LOG(INFO) << LOG_DESC("campaignLeader error")
                               << LOG_KV("msg", response.error_message())
                               << LOG_KV("code", response.error_code())
                               << LOG_KV("purpose", m_config->purpose())
                               << LOG_KV("lease", leaseID);
            tryToSwitchToBackup();
            return false;
        }
        m_campaignTimer->stop();
        ELECTION_LOG(INFO) << LOG_DESC("campaignLeader success")
                           << LOG_KV("leaderKey", m_config->leaderKey())
                           << LOG_KV("purpose", m_config->purpose()) << LOG_KV("lease", leaseID)
                           << LOG_KV("version", response.value().version())
                           << LOG_KV("msg", response.error_message())
                           << LOG_KV("value", response.value().as_string())
                           << LOG_KV("key", response.value().key());
        // cancel the old keepAlive
        if (m_keepAlive)
        {
            ELECTION_LOG(INFO) << LOG_DESC("campaignLeader: cancel keepAlive thread")
                               << LOG_KV("lease", m_keepAlive->Lease())
                               << LOG_KV("leaderKey", m_config->leaderKey());
            m_keepAlive->Cancel();
        }
        // establish new keepAlive
        auto keepAliveTTL = m_config->leaseTTL() - 1;
        m_keepAlive = std::make_shared<etcd::KeepAlive>(*(m_config->etcdClient()),
            boost::bind(&LeaderElection::onKeepAliveException, this, boost::placeholders::_1),
            keepAliveTTL, leaseID);
        if (m_onCampaignHandler)
        {
            m_onCampaignHandler(true);
        }
        ELECTION_LOG(INFO)
            << LOG_DESC("campaignLeader: establish new keepAlive thread and switch to master-node")
            << LOG_KV("ttl", keepAliveTTL) << LOG_KV("lease", leaseID)
            << LOG_KV("leaderKey", m_config->leaderKey());
        return true;
    }
    catch (std::exception const& e)
    {
        ELECTION_LOG(WARNING) << LOG_DESC("campaignLeader exception")
                              << LOG_KV("error", boost::diagnostic_information(e));
    }
    return false;
}

void LeaderElection::onKeepAliveException(std::exception_ptr _exception)
{
    try
    {
        if (_exception)
        {
            std::rethrow_exception(_exception);
        }
    }
    catch (const std::exception& e)
    {
        ELECTION_LOG(WARNING) << LOG_DESC("onKeepAliveException, restart campaign")
                              << LOG_KV("error", boost::diagnostic_information(e));
    }
    if (m_campaignTimer)
    {
        m_campaignTimer->restart();
    }
    if (m_onKeepAliveException)
    {
        m_onKeepAliveException(_exception);
    }
}

void LeaderElection::tryToSwitchToBackup()
{
    if (!m_onCampaignHandler)
    {
        return;
    }
    auto leader = m_config->getLeader();
    if (leader && m_config->self()->memberID() == leader->memberID())
    {
        ELECTION_LOG(INFO) << LOG_DESC("tryToSwitchToBackup failed for the node-self is leader")
                           << LOG_KV("id", leader->memberID());
        return;
    }
    ELECTION_LOG(INFO) << LOG_DESC("tryToSwitchToBackup")
                       << LOG_KV("memberID", m_config->self()->memberID())
                       << LOG_KV("leader", leader ? leader->memberID() : "no-leader");
    m_onCampaignHandler(false);
}
