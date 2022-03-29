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
#include "GroupTypeDef.h"
#include "bcos-framework/interfaces/protocol/Protocol.h"
#include "bcos-framework/interfaces/protocol/ProtocolInfo.h"
#include "bcos-framework/interfaces/protocol/ServiceDesc.h"
#include <bcos-utilities/Common.h>
#include <memory>
namespace bcos
{
namespace group
{
enum NodeCryptoType : uint32_t
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
      : m_nodeName(_nodeName),
        m_nodeCryptoType((NodeCryptoType)_type),
        m_nodeProtocol(std::make_shared<bcos::protocol::ProtocolInfo>())
    {}
    virtual ~ChainNodeInfo() {}

    virtual std::string const& nodeName() const { return m_nodeName; }
    virtual NodeCryptoType const& nodeCryptoType() const { return m_nodeCryptoType; }
    virtual std::string const& serviceName(bcos::protocol::ServiceType _type) const
    {
        if (!m_servicesInfo.count(_type))
        {
            return c_emptyServiceName;
        }
        return m_servicesInfo.at(_type);
    }

    virtual void setNodeName(std::string const& _nodeName) { m_nodeName = _nodeName; }
    virtual void setNodeCryptoType(NodeCryptoType const& _nodeType)
    {
        m_nodeCryptoType = _nodeType;
    }
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
    void setNodeType(bcos::protocol::NodeType _type) { m_nodeType = _type; }
    bcos::protocol::NodeType nodeType() const { return m_nodeType; }

    bcos::protocol::ProtocolInfo::ConstPtr nodeProtocol() const { return m_nodeProtocol; }
    void setNodeProtocol(bcos::protocol::ProtocolInfo&& _protocol)
    {
        *m_nodeProtocol = std::move(_protocol);
    }

    void setNodeProtocol(bcos::protocol::ProtocolInfo const& _protocol)
    {
        *m_nodeProtocol = _protocol;
    }

    virtual void setWasm(bool _wasm) { m_wasm = _wasm; }
    virtual void setSmCryptoType(bool _smCryptoType) { m_smCryptoType = _smCryptoType; }

    bool wasm() const { return m_wasm; }
    bool smCryptoType() const { return m_smCryptoType; }

protected:
    bool m_microService = false;
    // the node name
    std::string m_nodeName;
    NodeCryptoType m_nodeCryptoType;
    // the nodeType
    bcos::protocol::NodeType m_nodeType;
    // the nodeID
    std::string m_nodeID;

    // mapping of service to deployed machine
    ServicesInfo m_servicesInfo;
    // the ini config maintained by the node, use the iniConfig of the node if empty
    std::string m_iniConfig = "";
    std::string c_emptyServiceName = "";

    // the node protocol
    bcos::protocol::ProtocolInfo::Ptr m_nodeProtocol;

    bool m_wasm{false};
    bool m_smCryptoType{false};
};
inline std::string printNodeInfo(ChainNodeInfo::Ptr _nodeInfo)
{
    if (!_nodeInfo)
    {
        return "";
    }
    std::stringstream oss;
    oss << LOG_KV("name", _nodeInfo->nodeName())
        << LOG_KV("cryptoType", std::to_string((int32_t)_nodeInfo->nodeCryptoType()))
        << LOG_KV("nodeType", _nodeInfo->nodeType());

    return oss.str();
}
}  // namespace group
}  // namespace bcos