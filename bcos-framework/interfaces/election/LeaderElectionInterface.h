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
 * @brief the interface for LeaderElection
 * @file LeaderElectionInterface.h
 * @author: yujiechen
 * @date 2022-04-26
 */
#pragma once
#include "bcos-framework/interfaces/protocol/MemberInterface.h"
#include <memory>
namespace bcos
{
namespace election
{
class LeaderElectionInterface
{
public:
    using Ptr = std::shared_ptr<LeaderElectionInterface>;
    LeaderElectionInterface() = default;
    virtual ~LeaderElectionInterface() {}

    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void updateSelfConfig(bcos::protocol::MemberInterface::Ptr _self) = 0;
    virtual bool electionClusterOk() const = 0;

    // called when campaign success, this logic should start to work when campaign success
    virtual void registerOnCampaignHandler(
        std::function<void(bool, bcos::protocol::MemberInterface::Ptr)> _onCampaignHandler) = 0;
    // called when keep-alive exception
    virtual void registerKeepAliveExceptionHandler(
        std::function<void(std::exception_ptr)> _handler) = 0;
    // handler called when the election cluster down
    virtual void registerOnElectionClusterException(std::function<void()> _handler) = 0;
    // handler called when the election cluster recover
    virtual void registerOnElectionClusterRecover(std::function<void()> _handler) = 0;
};

class LeaderElectionFactoryInterface
{
public:
    using Ptr = std::shared_ptr<LeaderElectionFactoryInterface>;
    LeaderElectionFactoryInterface() = default;
    virtual ~LeaderElectionFactoryInterface() {}
    virtual LeaderElectionInterface::Ptr createLeaderElection(std::string const& _memberID,
        std::string const& _memberConfig, std::string const& _etcdEndPoint,
        std::string const& _leaderKey, std::string const& _purpose, unsigned _leaseTTL) = 0;
};

}  // namespace election
}  // namespace bcos