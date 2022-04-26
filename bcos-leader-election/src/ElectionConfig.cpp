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
 * @brief the election config
 * @file ElectionConfig.cpp
 * @author: yujiechen
 * @date 2022-04-26
 */
#include "ElectionConfig.h"

using namespace bcos;
using namespace bcos::election;

void ElectionConfig::start()
{
    reCreateWatcher();
    if (!m_watcher)
    {
        return;
    }
    m_watcherTimer = std::make_shared<Timer>(5000);
    m_watcherTimer->registerTimeoutHandler(boost::bind(&ElectionConfig::refreshWatcher, this));
    m_watcherTimer->start();
}


void ElectionConfig::refreshWatcher()
{
    if (m_etcdClient->head().get().is_ok())
    {
        m_watcherTimer->restart();
        return;
    }
    m_watcherTimer->stop();
    ELECTION_LOG(INFO) << LOG_DESC("The client disconnect, wait for reconnect success");
    // wait until the client connects to etcd server
    while (!m_etcdClient->head().get().is_ok())
    {
        boost::this_thread::sleep_for(boost::chrono::milliseconds(100));
    }
    ELECTION_LOG(INFO) << LOG_DESC("The client reconnect success, refreshWatcher");
    reCreateWatcher();
    m_watcherTimer->start();
}