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
 * @file LeaderEntryPoint.h
 * @author: yujiechen
 * @date 2022-04-26
 */
#pragma once
#include <ElectionConfig.h>
#include <bcos-framework/interfaces/election/LeaderEntryPointInterface.h>
#include <memory>

namespace bcos
{
namespace election
{
class LeaderEntryPoint : public LeaderEntryPointInterface
{
public:
    using Ptr = std::shared_ptr<LeaderEntryPoint>;
    LeaderEntryPoint(ElectionConfig::Ptr _config) : m_config(_config) {}

    std::string const& leaderKey() const override { return m_config->leaseKey(); }
    bcos::protocol::MemberInterface::Ptr getLeader() override { return m_config->getLeader(); }

private:
    ElectionConfig::Ptr m_config;
};

class LeaderEntryPointFactoryImpl : public LeaderEntryPointFactory
{
public:
    using Ptr = std::shared_ptr<LeaderEntryPointFactoryImpl>();
    LeaderEntryPointFactoryImpl(bcos::protocol::MemberFactoryInterface::Ptr _memberFactory)
      : m_memberFactory(_memberFactory)
    {}
    ~LeaderEntryPointFactoryImpl() override {}

    LeaderEntryPointInterface::Ptr createLeaderEntryPoint(std::string const& _etcdEndPoint,
        std::string const& _leaderKey, std::string const& _purpose) override
    {
        auto config =
            std::make_shared<ElectionConfig>(_etcdEndPoint, m_memberFactory, _leaderKey, _purpose);
        ELECTION_LOG(INFO) << LOG_DESC("createLeaderEntryPoint")
                           << LOG_KV("etcdAddr", _etcdEndPoint) << LOG_KV("leaderKey", _leaderKey)
                           << LOG_KV("purpose", _purpose);
        return std::make_shared<LeaderEntryPoint>(config);
    }

private:
    bcos::protocol::MemberFactoryInterface::Ptr m_memberFactory;
};
}  // namespace election
}  // namespace bcos