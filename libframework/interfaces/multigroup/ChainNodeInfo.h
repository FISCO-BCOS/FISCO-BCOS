/**
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
 * @brief the information used to deploy new node
 * @file NodeInfo.h
 * @author: yujiechen
 * @date 2021-09-08
 */
#pragma once
#include "../../libutilities/Common.h"
#include "../protocol/ServiceDesc.h"
#include "GroupTypeDef.h"
#include <json/json.h>
#include <memory>
namespace bcos
{
namespace group
{
enum NodeType : uint32_t
{
    NON_SM_NODE = 0,
    SM_NODE = 1,
};
class ChainNodeInfo
{
public:
    using Ptr = std::shared_ptr<ChainNodeInfo>;
    using ConstPtr = std::shared_ptr<const ChainNodeInfo>;
    using ServicesInfo = std::map<bcos::protocol::ServiceType, std::string>;
    ChainNodeInfo() = default;
    ChainNodeInfo(std::string const& _nodeName, int32_t _type)
      : m_nodeName(_nodeName), m_nodeType((NodeType)_type)
    {}
    explicit ChainNodeInfo(std::string const& _jsonGroupInfoStr) { deserialize(_jsonGroupInfoStr); }
    virtual ~ChainNodeInfo() {}

    virtual std::string const& nodeName() const { return m_nodeName; }
    virtual NodeType const& nodeType() const { return m_nodeType; }
    virtual std::string const& serviceName(bcos::protocol::ServiceType _type) const
    {
        if (!m_servicesInfo.count(_type))
        {
            return c_emptyServiceName;
        }
        return m_servicesInfo.at(_type);
    }

    virtual void setNodeName(std::string const& _nodeName) { m_nodeName = _nodeName; }
    virtual void setNodeType(NodeType const& _nodeType) { m_nodeType = _nodeType; }
    virtual void appendServiceInfo(
        bcos::protocol::ServiceType _type, std::string const& _serviceName)
    {
        m_servicesInfo[_type] = _serviceName;
    }

    virtual ServicesInfo const& serviceInfo() const { return m_servicesInfo; }

    virtual void setServicesInfo(ServicesInfo&& _servicesInfo)
    {
        m_servicesInfo = std::move(_servicesInfo);
    }

    virtual void setIniConfig(std::string const& _iniConfig) { m_iniConfig = _iniConfig; }
    virtual std::string const& iniConfig() const { return m_iniConfig; }
    virtual std::string const& nodeID() const { return m_nodeID; }
    virtual void setNodeID(std::string const& _nodeID) { m_nodeID = _nodeID; }

    void setMicroService(bool _microService) { m_microService = _microService; }
    bool microService() const { return m_microService; }

    virtual void deserialize(const std::string& _json)
    {
        Json::Value value;
        Json::Reader jsonReader;
        if (!jsonReader.parse(_json, value))
        {
            BOOST_THROW_EXCEPTION(InvalidChainNodeInfo() << errinfo_comment(
                                      "The chain node information must be valid json string."));
        }
        // required: parse nodeName
        if (!value.isMember("name"))
        {
            BOOST_THROW_EXCEPTION(InvalidChainNodeInfo() << errinfo_comment(
                                      "The chain node information must set the chain node name."));
        }
        setNodeName(value["name"].asString());

        // required: parse nodeType
        if (!value.isMember("type"))
        {
            BOOST_THROW_EXCEPTION(InvalidChainNodeInfo() << errinfo_comment(
                                      "The chain node information must set the chain node type."));
        }
        NodeType type = (NodeType)(value["type"].asUInt());
        setNodeType(type);

        // required: parse iniConfig
        if (!value.isMember("iniConfig"))
        {
            BOOST_THROW_EXCEPTION(InvalidChainNodeInfo() << errinfo_comment(
                                      "The chain node information must set the init config info"));
        }
        setIniConfig(value["iniConfig"].asString());

        // required: parse deployInfo
        if (!value.isMember("serviceInfo"))
        {
            BOOST_THROW_EXCEPTION(InvalidChainNodeInfo() << errinfo_comment(
                                      "The chain node information must set the service info"));
        }

        if (!value["serviceInfo"].isArray())
        {
            BOOST_THROW_EXCEPTION(
                InvalidChainNodeInfo() << errinfo_comment("The service info must be array."));
        }

        auto const& serviceInfo = value["serviceInfo"];
        for (Json::ArrayIndex i = 0; i < serviceInfo.size(); i++)
        {
            auto const& serviceInfoItem = serviceInfo[i];
            if (!serviceInfoItem.isObject() || !serviceInfoItem.isMember("type") ||
                !serviceInfoItem.isMember("serviceName"))
            {
                BOOST_THROW_EXCEPTION(
                    InvalidChainNodeInfo() << errinfo_comment(
                        "Invalid service info: must contain the service type and name"));
            }
            appendServiceInfo((bcos::protocol::ServiceType)serviceInfoItem["type"].asInt(),
                serviceInfoItem["serviceName"].asString());
        }

        // optional: parse m_nodeID
        if (value.isMember("nodeID"))
        {
            setNodeID(value["nodeID"].asString());
        }

        // optional: parse microService
        if (value.isMember("microService"))
        {
            setMicroService(value["microService"].asBool());
        }
    }

    virtual Json::Value serialize()
    {
        Json::Value jResp;
        jResp["name"] = nodeName();
        jResp["type"] = nodeType();
        jResp["iniConfig"] = iniConfig();
        // set deployInfo
        jResp["serviceInfo"] = Json::Value(Json::arrayValue);

        auto const& infos = serviceInfo();
        for (auto const& innerIt : infos)
        {
            Json::Value item;
            item["type"] = innerIt.first;
            item["serviceName"] = innerIt.second;
            jResp["serviceInfo"].append(item);
        }

        return jResp;
    }

private:
    bool m_microService = false;
    // the node name
    std::string m_nodeName;
    NodeType m_nodeType;
    // the nodeID
    std::string m_nodeID;

    // mapping of service to deployed machine
    ServicesInfo m_servicesInfo;
    // the ini config maintained by the node, use the iniConfig of the node if empty
    std::string m_iniConfig = "";
    std::string c_emptyServiceName = "";
};
inline std::string printNodeInfo(ChainNodeInfo::Ptr _nodeInfo)
{
    if (!_nodeInfo)
    {
        return "";
    }
    std::stringstream oss;
    oss << LOG_KV("name", _nodeInfo->nodeName())
        << LOG_KV("type", std::to_string((int32_t)_nodeInfo->nodeType()));
    return oss.str();
}
}  // namespace group
}  // namespace bcos