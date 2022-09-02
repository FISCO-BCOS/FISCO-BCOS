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
 * @file CampaignConfig.cpp
 * @author: yujiechen
 * @date 2022-04-26
 */

#include "CampaignConfig.h"

using namespace bcos;
using namespace bcos::election;

bcos::protocol::MemberInterface::Ptr CampaignConfig::fetchLeader()
{
    auto leader = getLeader();
    if (leader)
    {
        return leader;
    }
    // TODO: check the mode is sync or async
    fetchLeaderInfoFromEtcd();
    return m_leader;
}

bcos::protocol::MemberInterface::Ptr CampaignConfig::getLeader()
{
    ReadGuard l(x_leader);
    return m_leader;
}

void CampaignConfig::fetchLeaderInfoFromEtcd()
{
    try
    {
        ELECTION_LOG(INFO) << LOG_DESC("fetchLeaderInfoFromEtcd") << LOG_KV("key", m_leaderKey);
        auto response = m_etcdClient->get(m_leaderKey).get();
        if (!checkAndUpdateLeaderKey(response))
        {
            ELECTION_LOG(INFO) << LOG_DESC("fetchLeaderInfoFromEtcd failed")
                               << LOG_KV("key", m_leaderKey);
            return;
        }
        ELECTION_LOG(INFO) << LOG_DESC("fetchLeaderInfoFromEtcd success")
                           << LOG_KV("leaderKey", m_leaderKey)
                           << LOG_KV("leaderID", m_leader->memberID());
    }
    catch (std::exception const& e)
    {
        ELECTION_LOG(WARNING) << LOG_DESC("fetchLeaderInfoFromEtcd exception")
                              << LOG_KV("leaderKey", m_leaderKey)
                              << LOG_KV("error", boost::diagnostic_information(e));
    }
}

void CampaignConfig::resetLeader(bcos::protocol::MemberInterface::Ptr _leader)
{
    WriteGuard l(x_leader);
    m_leader = _leader;
}

bool CampaignConfig::checkAndUpdateLeaderKey(etcd::Response _response)
{
    if (!_response.is_ok())
    {
        if (_response.error_code() != etcdv3::ERROR_KEY_NOT_FOUND)
        {
            ELECTION_LOG(WARNING) << LOG_DESC("checkAndUpdateLeaderKey error")
                                  << LOG_KV("code", _response.error_code())
                                  << LOG_KV("msg", _response.error_message())
                                  << LOG_KV("key", m_leaderKey);
            return false;
        }
        resetLeader(nullptr);
        // the key has already been cleared or not has been set, calls m_triggerCampaign
        if (m_triggerCampaign)
        {
            ELECTION_LOG(INFO) << LOG_DESC(
                                      "checkAndUpdateLeaderKey: the leader key is not accupied "
                                      "now, trigger campaign")
                               << LOG_KV("key", m_leaderKey);
            m_triggerCampaign();
        }
        return false;
    }
    auto valueVersion = _response.value().version();
    if (valueVersion == 0)
    {
        resetLeader(nullptr);
        auto ret = m_triggerCampaign();
        ELECTION_LOG(WARNING) << LOG_DESC("The leader key is released now, trigger campaign")
                              << LOG_KV("key", m_leaderKey) << LOG_KV("success", ret);
        return false;
    }
    auto leader = m_memberFactory->createMember(_response.value().as_string());
    auto seq = _response.value().modified_index();
    leader->setSeq(seq);
    leader->setLeaseID(_response.value().lease());
    resetLeader(leader);
    ELECTION_LOG(INFO) << LOG_DESC("checkAndUpdateLeaderKey success")
                       << LOG_KV("leaderKey", m_leaderKey) << LOG_KV("leader", m_leader->memberID())
                       << LOG_KV("version", valueVersion) << LOG_KV("modifiedIndex", seq)
                       << LOG_KV("lease", leader->leaseID());
    return true;
}

// Note: this handler is triggered when leaderKey changed
void CampaignConfig::onLeaderKeyChanged(etcd::Response _response)
{
    ELECTION_LOG(INFO) << LOG_DESC("onLeaderKeyChanged, checkAndUpdateLeaderKey")
                       << LOG_KV("leaderKey", m_leaderKey);
    checkAndUpdateLeaderKey(_response);
}


void CampaignConfig::onElectionClusterRecover()
{
    ElectionConfig::onElectionClusterRecover();
    // trigger camppagin
    resetLeader(nullptr);
    // calls m_triggerCampaign to elect new leader when election cluster recover
    if (m_triggerCampaign)
    {
        ELECTION_LOG(INFO) << LOG_DESC("onElectionClusterRecover: trigger campaign")
                           << LOG_KV("key", m_leaderKey);
        m_triggerCampaign();
    }
}