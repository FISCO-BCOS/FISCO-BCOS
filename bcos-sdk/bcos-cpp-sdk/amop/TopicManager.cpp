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
 * @file TopicManager.cpp
 * @author: octopus
 * @date 2021-08-26
 */

#include <bcos-cpp-sdk/amop/TopicManager.h>
#include <json/json.h>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::cppsdk::amop;

bool TopicManager::addTopic(const std::string& _topic)
{
    boost::unique_lock<boost::shared_mutex> lock(x_topics);
    auto result = m_topics.insert(_topic);
    return result.second;
}
bool TopicManager::addTopics(const std::set<std::string>& _topics)
{
    boost::unique_lock<boost::shared_mutex> lock(x_topics);
    auto oldSize = m_topics.size();
    m_topics.insert(_topics.begin(), _topics.end());
    return m_topics.size() > oldSize;
}
bool TopicManager::removeTopic(const std::string& _topic)
{
    boost::unique_lock<boost::shared_mutex> lock(x_topics);
    auto result = m_topics.erase(_topic);
    return result > 0;
}
bool TopicManager::removeTopics(const std::set<std::string>& _topics)
{
    size_t removeCount = 0;
    for (auto& topic : _topics)
    {
        auto r = removeTopic(topic);
        removeCount += (r ? 1 : 0);
    }
    return removeCount > 0;
}
std::set<std::string> TopicManager::topics() const
{
    boost::shared_lock<boost::shared_mutex> lock(x_topics);
    return m_topics;
}

std::string TopicManager::toJson()
{
    auto totalTopics = topics();
    Json::Value jTopics(Json::arrayValue);
    for (const auto& topic : totalTopics)
    {
        jTopics.append(topic);
    }
    Json::Value jReq;
    jReq["topics"] = jTopics;
    Json::FastWriter writer;
    std::string request = writer.write(jReq);
    return request;
}