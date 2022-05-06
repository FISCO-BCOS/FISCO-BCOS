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
 * @brief the config for the watcher
 * @file WatcherConfig.cpp
 * @author: yujiechen
 * @date 2022-04-28
 */

#include "WatcherConfig.h"

using namespace bcos;
using namespace bcos::election;

void WatcherConfig::reCreateWatcher()
{
    ELECTION_LOG(INFO) << LOG_DESC("reCreateWatcher");
    // Note: set recursive to watch subdirectory change
    m_watcher = std::make_shared<etcd::Watcher>(*m_etcdClient, m_watchDir,
        boost::bind(&WatcherConfig::onWatcherKeyChanged, this, boost::placeholders::_1), true);
}

void WatcherConfig::fetchLeadersInfo()
{
    ELECTION_LOG(INFO) << LOG_DESC("fetchLeadersInfo") << LOG_KV("watchDir", m_watchDir);
    auto response = m_etcdClient->ls(m_watchDir).get();
    if (!response.is_ok())
    {
        ELECTION_LOG(WARNING) << LOG_DESC("fetchLeadersInfo failed")
                              << LOG_KV("watchDir", m_watchDir);
        return;
    }
    auto const& values = response.values();
    for (auto const& value : values)
    {
        updateLeaderInfo(value);
    }
    ELECTION_LOG(INFO) << LOG_DESC("fetchLeadersInfo success") << LOG_KV("watchDir", m_watchDir);
}

void WatcherConfig::updateLeaderInfo(etcd::Value const& _value)
{
    try
    {
        auto version = _value.version();
        if (version == 0)
        {
            ELECTION_LOG(INFO) << LOG_DESC("updateLeaderInfo: the leaderKey has been released")
                               << LOG_KV("leaderKey", _value.key());
            {
                auto const& leaderKey = _value.key();
                UpgradableGuard l(x_keyToLeader);
                if (!m_keyToLeader.count(leaderKey))
                {
                    return;
                }
                auto member = m_keyToLeader.at(leaderKey);
                UpgradeGuard ul(l);
                m_keyToLeader.erase(leaderKey);
                onMemberDeleted(leaderKey, member);
            }
            return;
        }
        auto const& leaderKey = _value.key();
        auto member = m_memberFactory->createMember(_value.as_string());
        auto seq = _value.modified_index();
        member->setSeq(seq);
        ELECTION_LOG(INFO) << LOG_DESC("updateLeaderInfo: update leader")
                           << LOG_KV("leaderKey", leaderKey) << LOG_KV("member", member->memberID())
                           << LOG_KV("modifiedIndex", seq);
        {
            WriteGuard l(x_keyToLeader);
            m_keyToLeader[leaderKey] = member;
        }
        callNotificationHandlers(leaderKey, member);
    }
    catch (std::exception const& e)
    {
        ELECTION_LOG(WARNING) << LOG_DESC("updateLeaderInfo exception")
                              << LOG_KV("watchDir", m_watchDir) << LOG_KV("key", _value.key())
                              << LOG_KV("value", _value.as_string())
                              << LOG_KV("error", boost::diagnostic_information(e));
    }
}

void WatcherConfig::onWatcherKeyChanged(etcd::Response _response)
{
    if (!_response.is_ok())
    {
        ELECTION_LOG(WARNING) << LOG_DESC("onWatcherKeyChanged error")
                              << LOG_KV("code", _response.error_code())
                              << LOG_KV("msg", _response.error_message());
    }
    ELECTION_LOG(INFO) << LOG_DESC("onWatcherKeyChanged") << LOG_KV("key", _response.value().key())
                       << LOG_KV("version", _response.value().version());
    updateLeaderInfo(_response.value());
}

void WatcherConfig::callNotificationHandlers(
    std::string const& _key, bcos::protocol::MemberInterface::Ptr _member)
{
    ReadGuard l(x_notificationHandlers);
    for (auto const& handler : m_notificationHandlers)
    {
        try
        {
            handler(_key, _member);
        }
        catch (std::exception const& e)
        {
            ELECTION_LOG(ERROR) << LOG_DESC("callNotificationHandlers exception")
                                << LOG_KV("key", _key) << LOG_KV("memberID", _member->memberID())
                                << LOG_KV("error", boost::diagnostic_information(e));
        }
    }
}

void WatcherConfig::onMemberDeleted(
    std::string const& _key, bcos::protocol::MemberInterface::Ptr _member)
{
    ReadGuard l(x_onMemberDeleted);
    for (auto const& handler : m_onMemberDeleted)
    {
        try
        {
            handler(_key, _member);
        }
        catch (std::exception const& e)
        {
            ELECTION_LOG(ERROR) << LOG_DESC("onMemberDeleted exception") << LOG_KV("key", _key)
                                << LOG_KV("memberID", _member->memberID())
                                << LOG_KV("error", boost::diagnostic_information(e));
        }
    }
}