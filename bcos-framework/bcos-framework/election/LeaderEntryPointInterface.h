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
 * @brief the leader entry-point
 * @file LeaderEntryPointInterface.h
 * @author: yujiechen
 * @date 2022-04-26
 */
#pragma once
#include "bcos-framework/protocol/MemberInterface.h"
#include <memory>
namespace bcos
{
namespace election
{
class LeaderEntryPointInterface
{
public:
    using Ptr = std::shared_ptr<LeaderEntryPointInterface>;
    LeaderEntryPointInterface() = default;
    virtual ~LeaderEntryPointInterface() {}

    virtual bcos::protocol::MemberInterface::Ptr getLeaderByKey(std::string const& _leaderKey) = 0;
    virtual std::map<std::string, bcos::protocol::MemberInterface::Ptr> getAllLeaders() = 0;

    virtual void addMemberChangeNotificationHandler(
        std::function<void(std::string const&, bcos::protocol::MemberInterface::Ptr)>) = 0;
    virtual void addMemberDeleteNotificationHandler(
        std::function<void(std::string const&, bcos::protocol::MemberInterface::Ptr)> _handler) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
};

class LeaderEntryPointFactory
{
public:
    using Ptr = std::shared_ptr<LeaderEntryPointFactory>();
    LeaderEntryPointFactory() = default;
    virtual ~LeaderEntryPointFactory() {}

    virtual LeaderEntryPointInterface::Ptr createLeaderEntryPoint(std::string const& _etcdEndPoint,
        std::string const& _watchDir, std::string const& _purpose) = 0;
};
}  // namespace election
}  // namespace bcos