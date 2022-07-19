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
 * @brief the information used to manager group
 * @file JsonGroupInfoCodec.h
 * @author: yujiechen
 * @date 2021-09-08
 */
#pragma once
#include "JsonChainNodeInfoCodec.h"
#include <bcos-framework/multigroup/GroupInfoCodec.h>
#include <bcos-framework/multigroup/GroupInfoFactory.h>
#include <bcos-framework/multigroup/GroupTypeDef.h>
#include <json/json.h>
namespace bcos
{
namespace group
{
class JsonGroupInfoCodec : public GroupInfoCodec
{
public:
    using Ptr = std::shared_ptr<JsonGroupInfoCodec>;
    JsonGroupInfoCodec()
    {
        m_groupInfoFactory = std::make_shared<GroupInfoFactory>();
        m_chainNodeInfoCodec = std::make_shared<JsonChainNodeInfoCodec>();
    }
    ~JsonGroupInfoCodec() override {}

    GroupInfo::Ptr deserialize(const std::string& _json) override
    {
        auto groupInfo = m_groupInfoFactory->createGroupInfo();
        Json::Value root;
        Json::Reader jsonReader;

        if (!jsonReader.parse(_json, root))
        {
            BOOST_THROW_EXCEPTION(bcos::InvalidParameter() << bcos::errinfo_comment(
                                      "The group information must be valid json string."));
        }

        if (!root.isMember("chainID"))
        {
            BOOST_THROW_EXCEPTION(bcos::InvalidParameter() << bcos::errinfo_comment(
                                      "The group information must contain chainID"));
        }
        groupInfo->setChainID(root["chainID"].asString());

        if (!root.isMember("groupID"))
        {
            BOOST_THROW_EXCEPTION(bcos::InvalidParameter() << bcos::errinfo_comment(
                                      "The group information must contain groupID"));
        }
        groupInfo->setGroupID(root["groupID"].asString());

        if (root.isMember("genesisConfig"))
        {
            groupInfo->setGenesisConfig(root["genesisConfig"].asString());
        }


        if (!root.isMember("iniConfig"))
        {
            BOOST_THROW_EXCEPTION(bcos::InvalidParameter() << bcos::errinfo_comment(
                                      "The group information must contain iniConfig"));
        }
        groupInfo->setIniConfig(root["iniConfig"].asString());

        // nodeList
        if (!root.isMember("nodeList") || !root["nodeList"].isArray())
        {
            BOOST_THROW_EXCEPTION(bcos::InvalidParameter() << bcos::errinfo_comment(
                                      "The group information must contain nodeList"));
        }

        bool isFirst = true;
        for (Json::ArrayIndex i = 0; i < root["nodeList"].size(); ++i)
        {
            auto& nodeInfo = root["nodeList"][i];
            Json::FastWriter writer;
            std::string nodeStr = writer.write(nodeInfo);
            auto node = m_chainNodeInfoCodec->deserialize(nodeStr);
            groupInfo->appendNodeInfo(node);
            if (isFirst)
            {
                groupInfo->setWasm(node->wasm());
                groupInfo->setSmCryptoType(node->smCryptoType());
                isFirst = false;
            }
        }
        return groupInfo;
    }

    Json::Value serialize(GroupInfo::Ptr _groupInfo) override
    {
        Json::Value jResp;
        jResp["chainID"] = _groupInfo->chainID();
        jResp["groupID"] = _groupInfo->groupID();
        jResp["wasm"] = _groupInfo->wasm();
        jResp["smCryptoType"] = _groupInfo->smCryptoType();
        jResp["genesisConfig"] = _groupInfo->genesisConfig();
        jResp["iniConfig"] = _groupInfo->iniConfig();
        jResp["nodeList"] = Json::Value(Json::arrayValue);
        const auto& nodes = _groupInfo->nodeInfos();
        for (auto const& it : nodes)
        {
            jResp["nodeList"].append(m_chainNodeInfoCodec->serialize(it.second));
        }
        return jResp;
    }
    void serialize(std::string& _encodedData, GroupInfo::Ptr _groupInfo) override
    {
        Json::Value jResp;
        jResp["chainID"] = _groupInfo->chainID();
        jResp["groupID"] = _groupInfo->groupID();
        jResp["wasm"] = _groupInfo->wasm();
        jResp["smCryptoType"] = _groupInfo->smCryptoType();
        jResp["genesisConfig"] = _groupInfo->genesisConfig();
        jResp["iniConfig"] = _groupInfo->iniConfig();
        jResp["nodeList"] = Json::Value(Json::arrayValue);
        const auto& nodes = _groupInfo->nodeInfos();
        for (auto const& it : nodes)
        {
            jResp["nodeList"].append(m_chainNodeInfoCodec->serialize(it.second));
        }
        Json::FastWriter writer;
        _encodedData = writer.write(jResp);
    }

private:
    GroupInfoFactory::Ptr m_groupInfoFactory;
    JsonChainNodeInfoCodec::Ptr m_chainNodeInfoCodec;
};
}  // namespace group
}  // namespace bcos
