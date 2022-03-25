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
 * @date 2021-06-18
 */

#include <bcos-gateway/libamop/Common.h>
#include <bcos-gateway/libamop/TopicManager.h>
#include <json/json.h>
#include <algorithm>

using namespace bcos;
using namespace bcos::amop;
using namespace bcos::gateway;

/**
 * @brief: parse client sub topics json
 * @param _topicItems: return value, topics
 * @param _json: json
 * @return void
 */
bool TopicManager::parseSubTopicsJson(const std::string& _json, TopicItems& _topicItems)
{
    Json::Value root;
    Json::Reader jsonReader;

    try
    {
        if (!jsonReader.parse(_json, root))
        {
            TOPIC_LOG(ERROR) << LOG_BADGE("parseSubTopicsJson") << LOG_DESC("unable to parse json")
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

        TOPIC_LOG(INFO) << LOG_BADGE("parseSubTopicsJson")
                        << LOG_KV("topicItems size", topicItems.size()) << LOG_KV("json", _json);
        return true;
    }
    catch (const std::exception& e)
    {
        TOPIC_LOG(ERROR) << LOG_BADGE("parseSubTopicsJson")
                         << LOG_KV("error", boost::diagnostic_information(e))
                         << LOG_KV("json:", _json);
        return false;
    }
}

/**
 * @brief: client subscribe topic
 * @param _clientID: client identify, to be defined
 * @param _topicJson: topics subscribe by client
 * @return void
 */
void TopicManager::subTopic(const std::string& _client, const std::string& _topicJson)
{
    TopicItems topicItems;
    // parser topic json
    if (parseSubTopicsJson(_topicJson, topicItems))
    {
        subTopic(_client, topicItems);
    };
}

/**
 * @brief: client subscribe topic
 * @param _clientID: client identify, to be defined
 * @param _topicItems: topics subscribe by client
 * @return void
 */
void TopicManager::subTopic(const std::string& _client, const TopicItems& _topicItems)
{
    {
        std::unique_lock lock(x_clientTopics);
        m_client2TopicItems[_client] = _topicItems;  // Override the previous value
        incTopicSeq();
    }
    createAndGetServiceByClient(_client);
    TOPIC_LOG(INFO) << LOG_BADGE("subTopic") << LOG_KV("client", _client)
                    << LOG_KV("topicSeq", topicSeq())
                    << LOG_KV("topicItems size", _topicItems.size());
}

/**
 * @brief: query topics sub by client
 * @param _clientID: client identify, to be defined
 * @param _topicItems: topics subscribe by client
 * @return void
 */
bool TopicManager::queryTopicItemsByClient(const std::string& _client, TopicItems& _topicItems)
{
    bool result = false;
    {
        std::shared_lock lock(x_clientTopics);
        auto it = m_client2TopicItems.find(_client);
        if (it != m_client2TopicItems.end())
        {
            _topicItems = it->second;
            result = true;
        }
    }

    TOPIC_LOG(INFO) << LOG_BADGE("queryTopicItemsByClient") << LOG_KV("client", _client)
                    << LOG_KV("result", result) << LOG_KV("topicItems size", _topicItems.size());
    return result;
}

/**
 * @brief: clear all topics subscribe by client
 * @param _clientID: client identify, to be defined
 * @return void
 */
void TopicManager::removeTopics(
    const std::string& _client, std::vector<std::string> const& _topicList)
{
    if (_topicList.size() == 0)
    {
        return;
    }
    {
        std::unique_lock lock(x_clientTopics);
        if (!m_client2TopicItems.count(_client))
        {
            return;
        }
        for (auto const& topic : _topicList)
        {
            if (m_client2TopicItems[_client].count(topic))
            {
                m_client2TopicItems[_client].erase(topic);
            }
            TOPIC_LOG(INFO) << LOG_BADGE("removeTopics") << LOG_KV("client", _client)
                            << LOG_KV("topicSeq", topicSeq()) << LOG_KV("topic", topic);
        }
        incTopicSeq();
    }
}

void TopicManager::removeTopicsByClients(const std::vector<std::string>& _clients)
{
    if (_clients.size() == 0)
    {
        return;
    }
    std::unique_lock lock(x_clientTopics);
    for (auto const& client : _clients)
    {
        if (m_client2TopicItems.count(client))
        {
            m_client2TopicItems.erase(client);
        }
        TOPIC_LOG(INFO) << LOG_BADGE("removeTopicsByClients") << LOG_KV("client", client);
    }
    incTopicSeq();
}

/**
 * @brief: query topics subscribe by all connected clients
 * @return result in json format
 */
std::string TopicManager::queryTopicsSubByClient()
{
    try
    {
        uint32_t seq;
        TopicItems topicItems;
        {
            std::shared_lock lock(x_clientTopics);
            seq = topicSeq();
            for (const auto& m : m_client2TopicItems)
            {
                topicItems.insert(m.second.begin(), m.second.end());
            }
        }

        Json::Value jTopics = Json::Value(Json::arrayValue);
        for (const auto& topicItem : topicItems)
        {
            jTopics.append(topicItem.topicName());
        }

        Json::Value jResp;
        jResp["topicSeq"] = seq;
        jResp["topicItems"] = jTopics;

        Json::FastWriter writer;
        std::string topicJson = writer.write(jResp);

        TOPIC_LOG(DEBUG) << LOG_BADGE("queryTopicsSubByClient") << LOG_KV("topicSeq", seq)
                         << LOG_KV("topicJson", topicJson);
        return topicJson;
    }
    catch (const std::exception& e)
    {
        TOPIC_LOG(ERROR) << LOG_BADGE("queryTopicsSubByClient")
                         << LOG_KV("error", boost::diagnostic_information(e));
        return "";
    }
}

/**
 * @brief: parse json to fetch topicSeq and topicItems
 * @param _topicSeq: topicSeq
 * @param _topicItems: topics
 * @param _json: json
 * @return void
 */
bool TopicManager::parseTopicItemsJson(
    uint32_t& _topicSeq, TopicItems& _topicItems, const std::string& _json)
{
    Json::Value root;
    Json::Reader jsonReader;

    try
    {
        if (!jsonReader.parse(_json, root))
        {
            TOPIC_LOG(ERROR) << LOG_BADGE("parseTopicItemsJson") << LOG_DESC("unable to parse json")
                             << LOG_KV("json:", _json);
            return false;
        }

        uint32_t topicSeq;
        TopicItems topicItems;

        topicSeq = root["topicSeq"].asUInt();
        auto topicItemsSize = root["topicItems"].size();

        for (unsigned int i = 0; i < topicItemsSize; i++)
        {
            std::string topic = root["topicItems"][i].asString();
            topicItems.insert(TopicItem(topic));
        }

        _topicSeq = topicSeq;
        _topicItems = topicItems;

        TOPIC_LOG(INFO) << LOG_BADGE("parseTopicItemsJson") << LOG_KV("topicSeq", topicSeq)
                        << LOG_KV("topicItems size", topicItems.size()) << LOG_KV("json", _json);
        return true;
    }
    catch (const std::exception& e)
    {
        TOPIC_LOG(ERROR) << LOG_BADGE("parseTopicItemsJson") << LOG_DESC("parse json exception")
                         << LOG_KV("error", boost::diagnostic_information(e))
                         << LOG_KV("json:", _json);
        return false;
    }
}

/**
 * @brief: check if the topicSeq of nodeID changed
 * @param _nodeID: the peer nodeID
 * @param _topicSeq: the topicSeq of the nodeID
 * @return bool: if the nodeID has been changed
 */
bool TopicManager::checkTopicSeq(P2pID const& _nodeID, uint32_t _topicSeq)
{
    std::shared_lock lock(x_topics);
    auto it = m_nodeID2TopicSeq.find(_nodeID);
    if (it != m_nodeID2TopicSeq.end() && it->second == _topicSeq)
    {
        return false;
    }
    return true;
}

/**
 * @brief: update online nodeIDs, clean up the offline nodeIDs state
 * @param _nodeIDs: the online nodeIDs
 * @return void
 */
void TopicManager::notifyNodeIDs(const std::vector<P2pID>& _nodeIDs)
{
    int removeCount = 0;
    {
        std::unique_lock lock(x_topics);
        for (auto it = m_nodeID2TopicSeq.begin(); it != m_nodeID2TopicSeq.end();)
        {
            if (std::find_if(_nodeIDs.begin(), _nodeIDs.end(), [&it](std::string _nodeID) -> bool {
                    return it->first == _nodeID;
                }) == _nodeIDs.end())
            {  // nodeID is offline, remove the nodeID's state
                m_nodeID2TopicItems.erase(it->first);
                it = m_nodeID2TopicSeq.erase(it);
                removeCount++;
            }
            else
            {
                ++it;
            }
        }
    }

    TOPIC_LOG(INFO) << LOG_BADGE("notifyNodeIDs") << LOG_KV("removeCount", removeCount);
}

/**
 * @brief: update the topicSeq and topicItems of the nodeID's
 * @param _nodeID: nodeID
 * @param _topicSeq: topicSeq
 * @param _topicItems: topicItems
 * @return void
 */
void TopicManager::updateSeqAndTopicsByNodeID(
    P2pID const& _nodeID, uint32_t _topicSeq, const TopicItems& _topicItems)
{
    {
        std::unique_lock lock(x_topics);
        m_nodeID2TopicSeq[_nodeID] = _topicSeq;
        m_nodeID2TopicItems[_nodeID] = _topicItems;
    }

    TOPIC_LOG(INFO) << LOG_BADGE("updateSeqAndTopicsByNodeID") << LOG_KV("nodeID", _nodeID)
                    << LOG_KV("topicSeq", _topicSeq)
                    << LOG_KV("topicItems size", _topicItems.size());
}

/**
 * @brief: find the nodeIDs by topic
 * @param _topic: topic
 * @param _nodeIDs: nodeIDs
 * @return void
 */
void TopicManager::queryNodeIDsByTopic(
    const std::string& _topic, std::vector<std::string>& _nodeIDs)
{
    std::shared_lock lock(x_topics);
    for (auto it = m_nodeID2TopicItems.begin(); it != m_nodeID2TopicItems.end(); ++it)
    {
        auto findIt = std::find_if(it->second.begin(), it->second.end(),
            [_topic](const TopicItem& _topicItem) { return _topic == _topicItem.topicName(); });
        // only return the connected nodes
        if (findIt != it->second.end() && m_network->connected(it->first))
        {
            _nodeIDs.push_back(it->first);
        }
    }
    return;
}

/**
 * @brief: find clients by topic
 * @param _topic: topic
 * @param _nodeIDs: nodeIDs
 * @return void
 */
void TopicManager::queryClientsByTopic(
    const std::string& _topic, std::vector<std::string>& _clients)
{
    {
        std::shared_lock lock(x_clientTopics);
        for (const auto& items : m_client2TopicItems)
        {
            auto it = std::find_if(items.second.begin(), items.second.end(),
                [_topic](const TopicItem& _topicItem) { return _topic == _topicItem.topicName(); });
            if (it != items.second.end())
            {
                _clients.push_back(items.first);
            }
        }
    }

    TOPIC_LOG(INFO) << LOG_BADGE("queryClientsByTopic") << LOG_KV("topic", _topic)
                    << LOG_KV("clients size", _clients.size());
}

void TopicManager::notifyRpcToSubscribeTopics()
{
    try
    {
        auto servicePrx = Application::getCommunicator()->stringToProxy<bcostars::RpcServicePrx>(
            m_rpcServiceName);
        auto rpcClient = std::make_shared<bcostars::RpcServiceClient>(servicePrx, m_rpcServiceName);
        vector<EndpointInfo> activeEndPoints;
        vector<EndpointInfo> nactiveEndPoints;
        TOPIC_LOG(INFO) << LOG_DESC("notifyRpcToSubscribeTopics")
                        << LOG_KV("rpcServiceName", m_rpcServiceName)
                        << LOG_KV("activeEndPoints", activeEndPoints.size());
        rpcClient->prx()->tars_endpointsAll(activeEndPoints, nactiveEndPoints);
        for (auto const& endPoint : activeEndPoints)
        {
            auto endPointStr = m_rpcServiceName + "@tcp -h " + endPoint.getEndpoint().getHost() +
                               " -p " +
                               boost::lexical_cast<std::string>(endPoint.getEndpoint().getPort());
            auto servicePrx =
                Application::getCommunicator()->stringToProxy<bcostars::RpcServicePrx>(endPointStr);
            auto serviceClient =
                std::make_shared<bcostars::RpcServiceClient>(servicePrx, m_rpcServiceName);
            serviceClient->asyncNotifySubscribeTopic(
                [this, endPointStr](Error::Ptr _error, std::string _topicInfo) {
                    if (_error)
                    {
                        TOPIC_LOG(INFO) << LOG_DESC("asyncNotifySubscribeTopic error")
                                        << LOG_KV("endPoint", endPointStr)
                                        << LOG_KV("code", _error->errorCode())
                                        << LOG_KV("msg", _error->errorMessage());
                        return;
                    }
                    TOPIC_LOG(INFO)
                        << LOG_DESC("asyncNotifySubscribeTopic success")
                        << LOG_KV("endPoint", endPointStr) << LOG_KV("topicInfo", _topicInfo);
                    subTopic(endPointStr, _topicInfo);
                });
        }
    }
    catch (std::exception const& e)
    {
        TOPIC_LOG(WARNING) << LOG_DESC("notifyRpcToSubscribeTopics exception")
                           << LOG_KV("error", boost::diagnostic_information(e));
    }
}

void TopicManager::checkClientConnection()
{
    m_timer->restart();
    std::vector<std::string> clientsToRemove;
    {
        std::unique_lock lock(x_clientInfo);
        for (auto it = m_clientInfo.begin(); it != m_clientInfo.end();)
        {
            try
            {
                auto rpcClient = std::dynamic_pointer_cast<bcostars::RpcServiceClient>(it->second);
                rpcClient->prx()->tars_ping();
                it++;
                continue;
            }
            catch (std::exception const& e)
            {
                TOPIC_LOG(INFO) << LOG_DESC("checkClientConnection exception")
                                << LOG_KV("error", boost::diagnostic_information(e));
            }
            {
                TOPIC_LOG(INFO) << LOG_DESC("checkClientConnection: remove disconnected client")
                                << LOG_KV("client", it->first);
                clientsToRemove.emplace_back(it->first);
                it = m_clientInfo.erase(it);
            }
        }
        if (clientsToRemove.size() > 0)
        {
            removeTopicsByClients(clientsToRemove);
        }
    }
}