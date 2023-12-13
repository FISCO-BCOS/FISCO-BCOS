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
 * @file GroupInfo.h
 * @author: yujiechen
 * @date 2021-09-08
 */
#pragma once
#include "ChainNodeInfoFactory.h"
#include "GroupTypeDef.h"
namespace bcos::group
{
class GroupInfo
{
public:
    using Ptr = std::shared_ptr<GroupInfo>;
    GroupInfo() = default;
    GroupInfo(std::string _chainID, std::string _groupID)
      : m_chainID(std::move(_chainID)), m_groupID(std::move(_groupID))
    {}
    virtual ~GroupInfo() = default;

    virtual std::string const& genesisConfig() const { return m_genesisConfig; }
    virtual std::string const& iniConfig() const { return m_iniConfig; }
    virtual ChainNodeInfo::Ptr nodeInfo(std::string_view _nodeName) const
    {
        ReadGuard l(x_nodeInfos);
        auto it = m_nodeInfos.find(_nodeName);
        if (it == m_nodeInfos.end())
        {
            return nullptr;
        }
        return it->second;
    }

    std::string const& groupID() const { return m_groupID; }
    std::string const& chainID() const { return m_chainID; }

    virtual void setGenesisConfig(std::string const& _genesisConfig)
    {
        m_genesisConfig = _genesisConfig;
    }
    virtual void setIniConfig(std::string const& _iniConfig) { m_iniConfig = _iniConfig; }
    virtual bool appendNodeInfo(ChainNodeInfo::Ptr _nodeInfo)
    {
        UpgradableGuard l(x_nodeInfos);
        auto const& nodeName = _nodeInfo->nodeName();
        auto it = m_nodeInfos.find(nodeName);
        if (it != m_nodeInfos.end())
        {
            return false;
        }
        UpgradeGuard ul(l);
        m_nodeInfos[nodeName] = _nodeInfo;
        return true;
    }

    virtual void updateNodeInfo(ChainNodeInfo::Ptr _nodeInfo)
    {
        WriteGuard l(x_nodeInfos);
        auto const& nodeName = _nodeInfo->nodeName();
        auto it = m_nodeInfos.find(nodeName);
        if (it != m_nodeInfos.end())
        {
            *(it->second) = *_nodeInfo;
            return;
        }
        m_nodeInfos[nodeName] = _nodeInfo;
    }

    virtual bool removeNodeInfo(std::string const& _nodeName)
    {
        UpgradableGuard l(x_nodeInfos);
        auto it = m_nodeInfos.find(_nodeName);
        if (it == m_nodeInfos.end())
        {
            return false;
        }
        UpgradeGuard ul(l);
        m_nodeInfos.erase(it);
        return true;
    }

    virtual void setGroupID(std::string const& _groupID) { m_groupID = _groupID; }
    virtual void setChainID(std::string const& _chainID) { m_chainID = _chainID; }
    virtual int64_t nodesNum() const
    {
        ReadGuard l(x_nodeInfos);
        return m_nodeInfos.size();
    }

    auto nodeInfos() const
    {
        ReadGuard l(x_nodeInfos);
        return m_nodeInfos;
    }

    // Use range with lock
    auto nodeInfoList() const
    {
        return m_nodeInfos |
               RANGES::views::transform([lock = std::make_shared<ReadGuard>(x_nodeInfos)](
                                            auto const& pair) -> auto& { return pair; });
    }

    bcos::group::ChainNodeInfoFactory::Ptr chainNodeInfoFactory() const
    {
        return m_chainNodeInfoFactory;
    }

    void setChainNodeInfoFactory(bcos::group::ChainNodeInfoFactory::Ptr _chainNodeInfoFactory)
    {
        m_chainNodeInfoFactory = std::move(_chainNodeInfoFactory);
    }

    bool wasm() const { return m_wasm; }
    bool smCryptoType() const { return m_smCryptoType; }
    virtual void setWasm(bool _wasm) { m_wasm = _wasm; }
    virtual void setSmCryptoType(bool _smCryptoType) { m_smCryptoType = _smCryptoType; }

private:
    bool m_wasm{false};
    bool m_smCryptoType{false};

    ChainNodeInfoFactory::Ptr m_chainNodeInfoFactory;

    std::string m_chainID;
    std::string m_groupID;
    // the genesis config for the group
    std::string m_genesisConfig;
    // the iniConfig for the group
    std::string m_iniConfig;
    // node name to node deployment information mapping
    std::map<std::string, ChainNodeInfo::Ptr, std::less<>> m_nodeInfos;
    mutable SharedMutex x_nodeInfos;
};

inline std::string printGroupInfo(const GroupInfo::Ptr& _groupInfo)
{
    if (!_groupInfo)
    {
        return "";
    }
    std::stringstream oss;
    oss << LOG_KV("group", _groupInfo->groupID()) << LOG_KV("chain", _groupInfo->chainID())
        << LOG_KV("nodeSize", _groupInfo->nodesNum());
    return oss.str();
}
}  // namespace bcos::group
