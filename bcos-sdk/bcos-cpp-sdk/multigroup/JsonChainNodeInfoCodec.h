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
 * @file JsonChainNodeInfoCodec.h
 * @author: yujiechen
 * @date 2021-09-08
 */
#pragma once
#include <bcos-framework/multigroup/ChainNodeInfoFactory.h>
#include <bcos-framework/multigroup/GroupTypeDef.h>
#include <bcos-framework/protocol/ServiceDesc.h>
#include <bcos-utilities/Common.h>
#include <json/json.h>
#include <memory>
namespace bcos
{
namespace group
{
class JsonChainNodeInfoCodec
{
public:
    using Ptr = std::shared_ptr<JsonChainNodeInfoCodec>;
    using ConstPtr = std::shared_ptr<const JsonChainNodeInfoCodec>;
    JsonChainNodeInfoCodec() { m_chainNodeInfoFactory = std::make_shared<ChainNodeInfoFactory>(); }
    virtual ~JsonChainNodeInfoCodec() {}

    virtual void deserializeIniConfig(ChainNodeInfo::Ptr _chainNodeInfo)
    {
        Json::Value value;
        Json::Reader jsonReader;
        if (!jsonReader.parse(_chainNodeInfo->iniConfig(), value))
        {
            BOOST_THROW_EXCEPTION(bcos::InvalidParameter() << bcos::errinfo_comment(
                                      "The chain node ini config must be valid json string."));
        }

        // required: isWasm
        if (!value.isMember("isWasm"))
        {
            BOOST_THROW_EXCEPTION(bcos::InvalidParameter() << bcos::errinfo_comment(
                                      "The chain node ini config must set is wasm info."));
        }
        _chainNodeInfo->setWasm(value["isWasm"].asBool());

        // required: smCryptoType
        if (!value.isMember("smCryptoType"))
        {
            BOOST_THROW_EXCEPTION(bcos::InvalidParameter() << bcos::errinfo_comment(
                                      "The chain node ini config must set sm crypto type."));
        }
        _chainNodeInfo->setSmCryptoType(value["smCryptoType"].asBool());
    }

    virtual ChainNodeInfo::Ptr deserialize(const std::string& _json)
    {
        auto chainNodeInfo = m_chainNodeInfoFactory->createNodeInfo();
        Json::Value value;
        Json::Reader jsonReader;
        if (!jsonReader.parse(_json, value))
        {
            BOOST_THROW_EXCEPTION(bcos::InvalidParameter() << bcos::errinfo_comment(
                                      "The chain node information must be valid json string."));
        }
        // required: parse nodeName
        if (!value.isMember("name"))
        {
            BOOST_THROW_EXCEPTION(bcos::InvalidParameter() << bcos::errinfo_comment(
                                      "The chain node information must set the chain node name."));
        }
        chainNodeInfo->setNodeName(value["name"].asString());

        // required: parse nodeType
        if (!value.isMember("type"))
        {
            BOOST_THROW_EXCEPTION(bcos::InvalidParameter() << bcos::errinfo_comment(
                                      "The chain node information must set the chain node type."));
        }
        NodeCryptoType type = (NodeCryptoType)(value["type"].asUInt());
        chainNodeInfo->setNodeCryptoType(type);

        // required: parse iniConfig
        if (!value.isMember("iniConfig"))
        {
            BOOST_THROW_EXCEPTION(bcos::InvalidParameter() << bcos::errinfo_comment(
                                      "The chain node information must set the init config info"));
        }
        chainNodeInfo->setIniConfig(value["iniConfig"].asString());
        deserializeIniConfig(chainNodeInfo);

        // required: parse deployInfo
        if (!value.isMember("serviceInfo"))
        {
            BOOST_THROW_EXCEPTION(bcos::InvalidParameter() << bcos::errinfo_comment(
                                      "The chain node information must set the service info"));
        }

        if (!value["serviceInfo"].isArray())
        {
            BOOST_THROW_EXCEPTION(bcos::InvalidParameter()
                                  << bcos::errinfo_comment("The service info must be array."));
        }

        auto const& serviceInfo = value["serviceInfo"];
        for (Json::ArrayIndex i = 0; i < serviceInfo.size(); i++)
        {
            auto const& serviceInfoItem = serviceInfo[i];
            if (!serviceInfoItem.isObject() || !serviceInfoItem.isMember("type") ||
                !serviceInfoItem.isMember("serviceName"))
            {
                BOOST_THROW_EXCEPTION(
                    bcos::InvalidParameter() << bcos::errinfo_comment(
                        "Invalid service info: must contain the service type and name"));
            }
            chainNodeInfo->appendServiceInfo(
                (bcos::protocol::ServiceType)serviceInfoItem["type"].asInt(),
                serviceInfoItem["serviceName"].asString());
        }

        // optional: parse m_nodeID
        if (value.isMember("nodeID"))
        {
            chainNodeInfo->setNodeID(value["nodeID"].asString());
        }

        // optional: parse microService
        if (value.isMember("microService"))
        {
            chainNodeInfo->setMicroService(value["microService"].asBool());
        }
        // optional: protocol info
        auto protocolInfo = std::make_shared<bcos::protocol::ProtocolInfo>();
        if (value.isMember("protocol"))
        {
            auto const& protocol = value["protocol"];
            if (protocol.isMember("minSupportedVersion"))
            {
                protocolInfo->setMinVersion(protocol["minSupportedVersion"].asUInt());
            }
            if (protocol.isMember("maxSupportedVersion"))
            {
                protocolInfo->setMaxVersion(protocol["maxSupportedVersion"].asUInt());
            }
            chainNodeInfo->setNodeProtocol(std::move(*protocolInfo));
            if (protocol.isMember("compatibilityVersion"))
            {
                chainNodeInfo->setCompatibilityVersion(protocol["compatibilityVersion"].asUInt());
            }
        }
        return chainNodeInfo;
    }

    virtual Json::Value serialize(ChainNodeInfo::Ptr _chainNodeInfo)
    {
        Json::Value jResp;
        jResp["name"] = _chainNodeInfo->nodeName();
        jResp["type"] = _chainNodeInfo->nodeCryptoType();
        jResp["iniConfig"] = _chainNodeInfo->iniConfig();
        // set deployInfo
        jResp["serviceInfo"] = Json::Value(Json::arrayValue);

        auto const& infos = _chainNodeInfo->serviceInfo();
        for (auto const& innerIt : infos)
        {
            Json::Value item;
            item["type"] = innerIt.first;
            item["serviceName"] = innerIt.second;
            jResp["serviceInfo"].append(item);
        }
        // set protocol info
        auto protocol = _chainNodeInfo->nodeProtocol();
        Json::Value protocolResponse;
        protocolResponse["minSupportedVersion"] = protocol->minVersion();
        protocolResponse["maxSupportedVersion"] = protocol->maxVersion();
        protocolResponse["compatibilityVersion"] = _chainNodeInfo->compatibilityVersion();
        jResp["protocol"] = protocolResponse;
        return jResp;
    }

private:
    ChainNodeInfoFactory::Ptr m_chainNodeInfoFactory;
};
}  // namespace group
}  // namespace bcos