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
 * @file ElectionConfig.h
 * @author: yujiechen
 * @date 2022-04-26
 */
#pragma once
#include "Common.h"
#include <bcos-framework/interfaces/protocol/MemberInterface.h>
#include <bcos-utilities/Timer.h>
#include <boost/bind/bind.hpp>
#include <etcd/Client.hpp>
#include <etcd/Watcher.hpp>
#include <memory>
namespace bcos
{
namespace election
{
class ElectionConfig : public std::enable_shared_from_this<ElectionConfig>
{
public:
    using Ptr = std::shared_ptr<ElectionConfig>;
    ElectionConfig(std::string const& _etcdEndPoint,
        bcos::protocol::MemberFactoryInterface::Ptr _memberFactory, std::string const& _purpose)
      : m_memberFactory(_memberFactory), m_purpose(_purpose)
    {
        m_etcdClient = std::make_shared<etcd::Client>(_etcdEndPoint);
    }

    virtual void start();
    virtual void stop()
    {
        if (m_watcherTimer)
        {
            m_watcherTimer->stop();
        }
        if (m_watcher)
        {
            m_watcher->Cancel();
        }
    }

    virtual ~ElectionConfig() { stop(); }

    std::string const purpose() const { return m_purpose; }
    std::shared_ptr<etcd::Client> etcdClient() { return m_etcdClient; }
    bcos::protocol::MemberFactoryInterface::Ptr memberFactory() { return m_memberFactory; }

    bool electionClusterOk() const { return m_electionClusterOk.load(); }
    void registerOnElectionClusterException(std::function<void()> _handler)
    {
        m_onElectionClusterException = _handler;
    }

    void registerOnElectionClusterRecover(std::function<void()> _handler)
    {
        m_onElectionClusterRecover = _handler;
    }

protected:
    virtual void refreshWatcher();
    virtual void reCreateWatcher() = 0;
    void onElectionClusterRecover();
    void onElectionClusterDown();

protected:
    std::shared_ptr<etcd::Client> m_etcdClient;
    std::shared_ptr<etcd::Watcher> m_watcher;

    bcos::protocol::MemberFactoryInterface::Ptr m_memberFactory;
    std::string m_purpose;

    // regularly check the etcdClient inventory, and reset the watcher after disconnection and
    // reconnection
    std::shared_ptr<Timer> m_watcherTimer;

    std::atomic_bool m_electionClusterOk = {true};

    // called when the election-cluster down
    std::function<void()> m_onElectionClusterException;
    // called when the election-cluster recover
    std::function<void()> m_onElectionClusterRecover;
};
}  // namespace election
}  // namespace bcos