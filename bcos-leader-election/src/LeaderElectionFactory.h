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
 * @brief factory to create leaderElection
 * @file LeaderElectionFactory.h
 * @author: yujiechen
 * @date 2022-04-26
 */
#pragma once
#include "LeaderElection.h"
#include <bcos-framework/interfaces/protocol/MemberInterface.h>
#include <memory>

namespace bcos
{
namespace election
{
class LeaderElectionFactory
{
public:
    using Ptr = std::shared_ptr<LeaderElectionFactory>;
    LeaderElectionFactory(bcos::protocol::MemberFactoryInterface::Ptr _memberFactory)
      : m_memberFactory(_memberFactory)
    {}
    virtual ~LeaderElectionFactory() {}

    LeaderElection::Ptr createLeaderElection(std::string const& _memberID,
        std::string const& _memberConfig, std::string const& _etcdEndPoint,
        std::string const& _leaderKey, std::string const& _purpose, unsigned _leaseTTL)
    {
        auto member = m_memberFactory->createMember();
        member->setMemberID(_memberID);
        member->setMemberConfig(_memberConfig);

        auto config = std::make_shared<CampaignConfig>(
            member, _etcdEndPoint, m_memberFactory, _leaderKey, _purpose, _leaseTTL);
        ELECTION_LOG(INFO) << LOG_DESC("createLeaderElection") << LOG_KV("memberID", _memberID)
                           << LOG_KV("etcdEndPoint", _etcdEndPoint)
                           << LOG_KV("leaderKey", _leaderKey) << LOG_KV("purpose", _purpose)
                           << LOG_KV("leaseTTL", _leaseTTL);
        return std::make_shared<LeaderElection>(config);
    }

private:
    bcos::protocol::MemberFactoryInterface::Ptr m_memberFactory;
};
}  // namespace election
}  // namespace bcos