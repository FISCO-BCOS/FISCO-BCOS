/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @file Common.h
 * @author: octopus
 * @date 2021-06-21
 */
#pragma once

#include <algorithm>
#include <chrono>
#include <ctime>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <vector>

#define TOPIC_LOG(LEVEL) BCOS_LOG(LEVEL) << "[AMOP][TOPIC]"
#define AMOP_MSG_LOG(LEVEL) BCOS_LOG(LEVEL) << "[AMOP][MSG]"
#define AMOP_LOG(LEVEL) BCOS_LOG(LEVEL) << "[AMOP][AMOP]"
namespace bcos
{
namespace amop
{
class TopicItem
{
public:
    using Ptr = std::shared_ptr<TopicItem>;

public:
    TopicItem() {}
    TopicItem(const std::string& _topicName) : m_topicName(_topicName) {}

private:
    std::string m_topicName;

public:
    std::string topicName() const { return m_topicName; }
    void setTopicName(const std::string& _topicName) { m_topicName = _topicName; }
};

inline bool operator<(const TopicItem& _topicItem0, const TopicItem& _topicItem1)
{
    return _topicItem0.topicName() < _topicItem1.topicName();
}
using TopicItems = std::set<TopicItem>;

inline std::string randomChoose(std::vector<std::string> _datas)
{
    auto seed = std::chrono::system_clock::now().time_since_epoch().count();
    std::default_random_engine e(seed);
    std::shuffle(_datas.begin(), _datas.end(), e);
    return *(_datas.begin());
}

inline std::string shortHex(std::string const& _nodeID)
{
    return _nodeID.substr(0, 8);
}
}  // namespace amop
}  // namespace bcos
