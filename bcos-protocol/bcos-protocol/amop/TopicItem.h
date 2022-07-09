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
 * @file TopicItem.h
 * @author: octopus
 * @date 2021-06-21
 */
#pragma once
#include <bcos-framework/interfaces/Common.h>
#include <json/json.h>
#include <memory>
#include <set>
#include <string>
#include <vector>
namespace bcos
{
namespace protocol
{
class TopicItem
{
public:
    using Ptr = std::shared_ptr<TopicItem>;
    TopicItem() {}
    TopicItem(const std::string& _topicName) : m_topicName(_topicName) {}
    std::string topicName() const { return m_topicName; }
    void setTopicName(const std::string& _topicName) { m_topicName = _topicName; }

private:
    std::string m_topicName;
};

inline bool operator<(const TopicItem& _topicItem0, const TopicItem& _topicItem1)
{
    return _topicItem0.topicName() < _topicItem1.topicName();
}
using TopicItems = std::set<TopicItem>;

inline bool parseSubTopicsJson(const std::string& _json, TopicItems& _topicItems)
{
    Json::Value root;
    Json::Reader jsonReader;

    try
    {
        if (!jsonReader.parse(_json, root))
        {
            BCOS_LOG(ERROR) << LOG_BADGE("parseSubTopicsJson") << LOG_DESC("unable to parse json")
                            << LOG_KV("json:", _json);
            return false;
        }

        TopicItems topicItems;

        auto topicItemsSize = root["topics"].size();

        for (unsigned int i = 0; i < topicItemsSize; i++)
        {
            std::string topic = root["topics"][i].asString();
            topicItems.insert(TopicItem(topic));
        }

        _topicItems = topicItems;

        BCOS_LOG(INFO) << LOG_BADGE("parseSubTopicsJson")
                       << LOG_KV("topicItems size", topicItems.size()) << LOG_KV("json", _json);
        return true;
    }
    catch (const std::exception& e)
    {
        BCOS_LOG(ERROR) << LOG_BADGE("parseSubTopicsJson")
                        << LOG_KV("error", boost::diagnostic_information(e))
                        << LOG_KV("json:", _json);
        return false;
    }
}
}  // namespace protocol
}  // namespace bcos
