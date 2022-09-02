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
 * @file WatcherConfig.h
 * @author: yujiechen
 * @date 2022-04-28
 */
#pragma once
#include "ElectionConfig.h"

namespace bcos
{
namespace election
{
class WatcherConfig : public ElectionConfig
{
public:
    using Ptr = std::shared_ptr<WatcherConfig>;
    WatcherConfig(std::string const& _etcdEndPoint, std::string const& _watchDir,
        bcos::protocol::MemberFactoryInterface::Ptr _memberFactory, std::string const& _purpose)
      : ElectionConfig(_etcdEndPoint, _memberFactory, _purpose)
    {
        m_watchDir = _watchDir;
        ELECTION_LOG(INFO) << LOG_DESC("WatcherConfig") << LOG_KV("watchDir", _watchDir);
    }

    ~WatcherConfig() override {}

    void start() override
    {
        ElectionConfig::start();
        fetchLeadersInfo();
    }

    std::string const& watchDir() const { return m_watchDir; }
    std::map<std::string, bcos::protocol::MemberInterface::Ptr> keyToLeader() const
    {
        ReadGuard l(x_keyToLeader);
        return m_keyToLeader;
    }

    bcos::protocol::MemberInterface::Ptr leader(std::string const& _key) const
    {
        ReadGuard l(x_keyToLeader);
        if (!m_keyToLeader.count(_key))
        {
            return nullptr;
        }
        return m_keyToLeader.at(_key);
    }

    void addMemberChangeNotificationHandler(
        std::function<void(std::string const&, bcos::protocol::MemberInterface::Ptr)> _handler)
    {
        ReadGuard l(x_notificationHandlers);
        m_notificationHandlers.emplace_back(_handler);
    }

    void addMemberDeleteNotificationHandler(
        std::function<void(std::string const&, bcos::protocol::MemberInterface::Ptr)> _handler)
    {
        ReadGuard l(x_onMemberDeleted);
        m_onMemberDeleted.emplace_back(_handler);
    }

protected:
    virtual void fetchLeadersInfo();
    void updateLeaderInfo(etcd::Value const& _value);

    void reCreateWatcher() override;
    virtual void onWatcherKeyChanged(etcd::Response _response);

    virtual void callNotificationHandlers(
        std::string const& _key, bcos::protocol::MemberInterface::Ptr _member);
    virtual void onMemberDeleted(
        std::string const& _key, bcos::protocol::MemberInterface::Ptr _member);

private:
    std::string m_watchDir;
    std::map<std::string, bcos::protocol::MemberInterface::Ptr> m_keyToLeader;
    mutable SharedMutex x_keyToLeader;

    std::vector<std::function<void(std::string const&, bcos::protocol::MemberInterface::Ptr)>>
        m_notificationHandlers;
    mutable SharedMutex x_notificationHandlers;

    std::vector<std::function<void(std::string const&, bcos::protocol::MemberInterface::Ptr)>>
        m_onMemberDeleted;
    mutable SharedMutex x_onMemberDeleted;
};
}  // namespace election
}  // namespace bcos