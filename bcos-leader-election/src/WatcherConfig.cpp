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
    m_watcher = std::make_shared<etcd::Watcher>(*m_etcdClient, m_watchDir,
        boost::bind(&WatcherConfig::onWatcherKeyChanged, this, boost::placeholders::_1));
}

void WatcherConfig::onWatcherKeyChanged(etcd::Response _response)
{
    if (!_response.is_ok())
    {
        ELECTION_LOG(WARNING) << LOG_DESC("onWatcherKeyChanged error")
                              << LOG_KV("code", _response.error_code())
                              << LOG_KV("msg", _response.error_message());
    }
    auto version = _response.value().version();
    if (version == 0)
    {
        ELECTION_LOG(INFO) << LOG_DESC("onWatcherKeyChanged: the leaderKey has been released")
                           << LOG_KV("leaderKey", _response.value().key());
        return;
    }
    auto leaderKey = _response.value().key();
    auto member = m_memberFactory->createMember(_response.value().as_string());
    auto seq = _response.value().modified_index();
    member->setSeq(seq);
    ELECTION_LOG(INFO) << LOG_DESC("onWatcherKeyChanged: update leader")
                       << LOG_KV("leaderKey", leaderKey) << LOG_KV("member", member->memberID())
                       << LOG_KV("modifiedIndex", seq);
    {
        WriteGuard l(x_keyToLeader);
        m_keyToLeader[leaderKey] = member;
    }
    callNotificationHandlers(leaderKey, member);
}