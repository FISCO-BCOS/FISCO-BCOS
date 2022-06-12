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
 * @file Common.h
 * @author: ancelmo
 * @date 2021-04-20
 */

#pragma once
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "bcos-framework//executor/ParallelTransactionExecutorInterface.h"
#include "bcos-tars-protocol/tars/GatewayInfo.h"
#include "bcos-tars-protocol/tars/GroupInfo.h"
#include "bcos-tars-protocol/tars/LedgerConfig.h"
#include "bcos-tars-protocol/tars/TransactionReceipt.h"
#include "bcos-tars-protocol/tars/TwoPCParams.h"
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <bcos-crypto/interfaces/crypto/KeyFactory.h>
#include <bcos-framework//consensus/ConsensusNode.h>
#include <bcos-framework//gateway/GatewayTypeDef.h>
#include <bcos-framework//ledger/LedgerConfig.h>
#include <bcos-framework//multigroup/ChainNodeInfoFactory.h>
#include <bcos-framework//multigroup/GroupInfoFactory.h>
#include <bcos-framework//protocol/LogEntry.h>
#include <bcos-framework//protocol/ProtocolInfo.h>
#include <bcos-utilities/Common.h>
#include <servant/Application.h>
#include <tup/Tars.h>
#include <cstdint>
#include <functional>
#include <memory>

namespace bcostars
{
namespace protocol
{
static bcos::crypto::HashType emptyHash;

class BufferWriterByteVector
{
protected:
    mutable std::vector<bcos::byte> _buffer;
    bcos::byte* _buf;
    std::size_t _len;
    std::size_t _buf_len;
    std::function<bcos::byte*(BufferWriterByteVector&, size_t)> _reserve;

private:
    BufferWriterByteVector(const BufferWriterByteVector&);
    BufferWriterByteVector& operator=(const BufferWriterByteVector& buf);

public:
    BufferWriterByteVector() : _buf(NULL), _len(0), _buf_len(0)
    {
#ifndef GEN_PYTHON_MASK
        _reserve = [](BufferWriterByteVector& os, size_t len) {
            os._buffer.resize(len);
            return os._buffer.data();
        };
#endif
    }

    ~BufferWriterByteVector() {}

    void reset() { _len = 0; }

    void writeBuf(const bcos::byte* buf, size_t len)
    {
        TarsReserveBuf(*this, _len + len);
        memcpy(_buf + _len, buf, len);
        _len += len;
    }

    const std::vector<bcos::byte>& getByteBuffer() const
    {
        _buffer.resize(_len);
        return _buffer;
    }
    std::vector<bcos::byte>& getByteBuffer()
    {
        _buffer.resize(_len);
        return _buffer;
    }
    const bcos::byte* getBuffer() const { return _buf; }
    size_t getLength() const { return _len; }
    void swap(std::vector<bcos::byte>& v)
    {
        _buffer.resize(_len);
        v.swap(_buffer);
        _buf = NULL;
        _buf_len = 0;
        _len = 0;
    }
    void swap(BufferWriterByteVector& buf)
    {
        buf._buffer.swap(_buffer);
        std::swap(_buf, buf._buf);
        std::swap(_buf_len, buf._buf_len);
        std::swap(_len, buf._len);
    }
};
}  // namespace protocol

inline bcos::group::ChainNodeInfo::Ptr toBcosChainNodeInfo(
    bcos::group::ChainNodeInfoFactory::Ptr _factory, bcostars::ChainNodeInfo const& _tarsNodeInfo)
{
    auto nodeInfo = _factory->createNodeInfo();
    nodeInfo->setNodeName(_tarsNodeInfo.nodeName);
    nodeInfo->setNodeCryptoType((bcos::group::NodeCryptoType)_tarsNodeInfo.nodeCryptoType);
    nodeInfo->setNodeID(_tarsNodeInfo.nodeID);
    nodeInfo->setIniConfig(_tarsNodeInfo.iniConfig);
    nodeInfo->setMicroService(_tarsNodeInfo.microService);
    nodeInfo->setNodeType((bcos::protocol::NodeType)_tarsNodeInfo.nodeType);
    for (auto const& it : _tarsNodeInfo.serviceInfo)
    {
        nodeInfo->appendServiceInfo((bcos::protocol::ServiceType)it.first, it.second);
    }
    // recover the nodeProtocolVersion
    auto& protocolInfo = _tarsNodeInfo.protocolInfo;
    auto bcosProtocolInfo = std::make_shared<bcos::protocol::ProtocolInfo>(
        (bcos::protocol::ProtocolModuleID)protocolInfo.moduleID,
        (bcos::protocol::ProtocolVersion)protocolInfo.minVersion,
        (bcos::protocol::ProtocolVersion)protocolInfo.maxVersion);
    nodeInfo->setNodeProtocol(std::move(*bcosProtocolInfo));
    // recover system version(data version)
    nodeInfo->setCompatibilityVersion(_tarsNodeInfo.compatibilityVersion);
    return nodeInfo;
}

inline bcos::group::GroupInfo::Ptr toBcosGroupInfo(
    bcos::group::ChainNodeInfoFactory::Ptr _nodeFactory,
    bcos::group::GroupInfoFactory::Ptr _groupFactory, bcostars::GroupInfo const& _tarsGroupInfo)
{
    auto groupInfo = _groupFactory->createGroupInfo();
    groupInfo->setChainID(_tarsGroupInfo.chainID);
    groupInfo->setGroupID(_tarsGroupInfo.groupID);
    groupInfo->setGenesisConfig(_tarsGroupInfo.genesisConfig);
    groupInfo->setIniConfig(_tarsGroupInfo.iniConfig);
    for (auto const& tarsNodeInfo : _tarsGroupInfo.nodeList)
    {
        groupInfo->appendNodeInfo(toBcosChainNodeInfo(_nodeFactory, tarsNodeInfo));
    }
    return groupInfo;
}

inline bcostars::ChainNodeInfo toTarsChainNodeInfo(bcos::group::ChainNodeInfo::Ptr _nodeInfo)
{
    bcostars::ChainNodeInfo tarsNodeInfo;
    if (!_nodeInfo)
    {
        return tarsNodeInfo;
    }
    tarsNodeInfo.nodeName = _nodeInfo->nodeName();
    tarsNodeInfo.nodeCryptoType = _nodeInfo->nodeCryptoType();
    tarsNodeInfo.nodeType = _nodeInfo->nodeType();
    auto const& info = _nodeInfo->serviceInfo();
    for (auto const& it : info)
    {
        tarsNodeInfo.serviceInfo[(int32_t)it.first] = it.second;
    }
    tarsNodeInfo.nodeID = _nodeInfo->nodeID();
    tarsNodeInfo.microService = _nodeInfo->microService();
    tarsNodeInfo.iniConfig = _nodeInfo->iniConfig();
    // set the nodeProtocolVersion
    auto const& protocol = _nodeInfo->nodeProtocol();
    tarsNodeInfo.protocolInfo.moduleID = protocol->protocolModuleID();
    tarsNodeInfo.protocolInfo.minVersion = protocol->minVersion();
    tarsNodeInfo.protocolInfo.maxVersion = protocol->maxVersion();
    // write the compatibilityVersion
    tarsNodeInfo.compatibilityVersion = _nodeInfo->compatibilityVersion();
    return tarsNodeInfo;
}

inline bcostars::GroupInfo toTarsGroupInfo(bcos::group::GroupInfo::Ptr _groupInfo)
{
    bcostars::GroupInfo tarsGroupInfo;
    if (!_groupInfo)
    {
        return tarsGroupInfo;
    }
    tarsGroupInfo.chainID = _groupInfo->chainID();
    tarsGroupInfo.groupID = _groupInfo->groupID();
    tarsGroupInfo.genesisConfig = _groupInfo->genesisConfig();
    tarsGroupInfo.iniConfig = _groupInfo->iniConfig();
    // set nodeList
    std::vector<bcostars::ChainNodeInfo> tarsNodeList;
    auto bcosNodeList = _groupInfo->nodeInfos();
    for (auto const& it : bcosNodeList)
    {
        auto const& nodeInfo = it.second;
        tarsNodeList.emplace_back(toTarsChainNodeInfo(nodeInfo));
    }
    tarsGroupInfo.nodeList = std::move(tarsNodeList);
    return tarsGroupInfo;
}

inline bcos::consensus::ConsensusNodeListPtr toConsensusNodeList(
    bcos::crypto::KeyFactory::Ptr _keyFactory,
    vector<bcostars::ConsensusNode> const& _tarsConsensusNodeList)
{
    auto consensusNodeList = std::make_shared<bcos::consensus::ConsensusNodeList>();
    for (auto const& node : _tarsConsensusNodeList)
    {
        auto nodeID = _keyFactory->createKey(
            bcos::bytesConstRef((bcos::byte*)node.nodeID.data(), node.nodeID.size()));
        consensusNodeList->push_back(
            std::make_shared<bcos::consensus::ConsensusNode>(nodeID, node.weight));
    }
    return consensusNodeList;
}

inline bcos::ledger::LedgerConfig::Ptr toLedgerConfig(
    bcostars::LedgerConfig const& _ledgerConfig, bcos::crypto::KeyFactory::Ptr _keyFactory)
{
    auto ledgerConfig = std::make_shared<bcos::ledger::LedgerConfig>();
    auto consensusNodeList = toConsensusNodeList(_keyFactory, _ledgerConfig.consensusNodeList);
    ledgerConfig->setConsensusNodeList(*consensusNodeList);

    auto observerNodeList = toConsensusNodeList(_keyFactory, _ledgerConfig.observerNodeList);
    ledgerConfig->setObserverNodeList(*observerNodeList);

    auto hash = bcos::crypto::HashType();
    if (_ledgerConfig.hash.size() >= bcos::crypto::HashType::SIZE)
    {
        hash = bcos::crypto::HashType(
            (const bcos::byte*)_ledgerConfig.hash.data(), bcos::crypto::HashType::SIZE);
    }
    ledgerConfig->setHash(hash);
    ledgerConfig->setBlockNumber(_ledgerConfig.blockNumber);
    ledgerConfig->setBlockTxCountLimit(_ledgerConfig.blockTxCountLimit);
    ledgerConfig->setLeaderSwitchPeriod(_ledgerConfig.leaderSwitchPeriod);
    ledgerConfig->setSealerId(_ledgerConfig.sealerId);
    ledgerConfig->setGasLimit(std::make_tuple(_ledgerConfig.gasLimit, _ledgerConfig.blockNumber));
    ledgerConfig->setCompatibilityVersion(_ledgerConfig.compatibilityVersion);
    return ledgerConfig;
}

inline vector<bcostars::ConsensusNode> toTarsConsensusNodeList(
    bcos::consensus::ConsensusNodeList const& _nodeList)
{
    // set consensusNodeList
    vector<bcostars::ConsensusNode> tarsConsensusNodeList;
    for (auto node : _nodeList)
    {
        bcostars::ConsensusNode consensusNode;
        consensusNode.nodeID.assign(node->nodeID()->data().begin(), node->nodeID()->data().end());
        consensusNode.weight = node->weight();
        tarsConsensusNodeList.emplace_back(consensusNode);
    }
    return tarsConsensusNodeList;
}
inline bcostars::LedgerConfig toTarsLedgerConfig(bcos::ledger::LedgerConfig::Ptr _ledgerConfig)
{
    bcostars::LedgerConfig ledgerConfig;
    auto hash = _ledgerConfig->hash().asBytes();
    ledgerConfig.hash.assign(hash.begin(), hash.end());
    ledgerConfig.blockNumber = _ledgerConfig->blockNumber();
    ledgerConfig.blockTxCountLimit = _ledgerConfig->blockTxCountLimit();
    ledgerConfig.leaderSwitchPeriod = _ledgerConfig->leaderSwitchPeriod();
    ledgerConfig.sealerId = _ledgerConfig->sealerId();
    ledgerConfig.gasLimit = std::get<0>(_ledgerConfig->gasLimit());
    ledgerConfig.compatibilityVersion = _ledgerConfig->compatibilityVersion();

    // set consensusNodeList
    ledgerConfig.consensusNodeList = toTarsConsensusNodeList(_ledgerConfig->consensusNodeList());
    // set observerNodeList
    ledgerConfig.observerNodeList = toTarsConsensusNodeList(_ledgerConfig->observerNodeList());
    return ledgerConfig;
}

inline bcostars::P2PInfo toTarsP2PInfo(bcos::gateway::P2PInfo const& _p2pInfo)
{
    bcostars::P2PInfo tarsP2PInfo;
    tarsP2PInfo.p2pID = _p2pInfo.p2pID;
    tarsP2PInfo.agencyName = _p2pInfo.agencyName;
    tarsP2PInfo.nodeName = _p2pInfo.nodeName;
    tarsP2PInfo.host = _p2pInfo.nodeIPEndpoint.address();
    tarsP2PInfo.port = _p2pInfo.nodeIPEndpoint.port();
    return tarsP2PInfo;
}

inline bcostars::GroupNodeInfo toTarsNodeIDInfo(
    std::string const& _groupID, std::set<std::string> const& _nodeIDList)
{
    GroupNodeInfo groupNodeIDInfo;
    groupNodeIDInfo.groupID = _groupID;
    groupNodeIDInfo.nodeIDList = std::vector<std::string>(_nodeIDList.begin(), _nodeIDList.end());
    return groupNodeIDInfo;
}
inline bcostars::GatewayInfo toTarsGatewayInfo(bcos::gateway::GatewayInfo::Ptr _bcosGatewayInfo)
{
    bcostars::GatewayInfo tarsGatewayInfo;
    if (!_bcosGatewayInfo)
    {
        return tarsGatewayInfo;
    }
    tarsGatewayInfo.p2pInfo = toTarsP2PInfo(_bcosGatewayInfo->p2pInfo());
    auto nodeIDList = _bcosGatewayInfo->nodeIDInfo();
    std::vector<GroupNodeInfo> tarsNodeIDInfos;
    for (auto const& it : nodeIDList)
    {
        tarsNodeIDInfos.emplace_back(toTarsNodeIDInfo(it.first, it.second));
    }
    tarsGatewayInfo.nodeIDInfo = tarsNodeIDInfos;
    return tarsGatewayInfo;
}

// Note: use struct here maybe Inconvenient to override
inline bcos::gateway::P2PInfo toBcosP2PNodeInfo(bcostars::P2PInfo const& _tarsP2pInfo)
{
    bcos::gateway::P2PInfo p2pInfo;
    p2pInfo.p2pID = _tarsP2pInfo.p2pID;
    p2pInfo.agencyName = _tarsP2pInfo.agencyName;
    p2pInfo.nodeName = _tarsP2pInfo.nodeName;
    p2pInfo.nodeIPEndpoint = bcos::gateway::NodeIPEndpoint(_tarsP2pInfo.host, _tarsP2pInfo.port);
    return p2pInfo;
}

inline bcos::gateway::GatewayInfo::Ptr fromTarsGatewayInfo(bcostars::GatewayInfo _tarsGatewayInfo)
{
    auto bcosGatewayInfo = std::make_shared<bcos::gateway::GatewayInfo>();
    auto p2pInfo = toBcosP2PNodeInfo(_tarsGatewayInfo.p2pInfo);
    std::map<std::string, std::set<std::string>> nodeIDInfos;
    for (auto const& it : _tarsGatewayInfo.nodeIDInfo)
    {
        auto const& nodeIDListInfo = it.nodeIDList;
        nodeIDInfos[it.groupID] =
            std::set<std::string>(nodeIDListInfo.begin(), nodeIDListInfo.end());
    }
    bcosGatewayInfo->setP2PInfo(std::move(p2pInfo));
    bcosGatewayInfo->setNodeIDInfo(std::move(nodeIDInfos));
    return bcosGatewayInfo;
}

template <typename T>
bool checkConnection(std::string const& _module, std::string const& _func, T prx,
    std::function<void(bcos::Error::Ptr)> _errorCallback, bool _callsErrorCallback = true)
{
    std::vector<tars::EndpointInfo> activeEndPoints;
    std::vector<tars::EndpointInfo> nactiveEndPoints;
    prx->tars_endpointsAll(activeEndPoints, nactiveEndPoints);
    if (activeEndPoints.size() > 0)
    {
        return true;
    }
    if (_errorCallback && _callsErrorCallback)
    {
        std::string errorMessage =
            _module + " calls interface " + _func + " failed for empty connection";
        _errorCallback(std::make_shared<bcos::Error>(-1, errorMessage));
    }
    return false;
}

inline bcostars::LogEntry toTarsLogEntry(bcos::protocol::LogEntry const& _logEntry)
{
    bcostars::LogEntry logEntry;
    logEntry.address.assign(_logEntry.address().begin(), _logEntry.address().end());
    for (auto& topicIt : _logEntry.topics())
    {
        logEntry.topic.push_back(std::vector<char>(topicIt.begin(), topicIt.end()));
    }
    logEntry.data.assign(_logEntry.data().begin(), _logEntry.data().end());
    return logEntry;
}

inline bcos::protocol::LogEntry toBcosLogEntry(bcostars::LogEntry const& _logEntry)
{
    std::vector<bcos::h256> topics;
    for (auto& topicIt : _logEntry.topic)
    {
        topics.emplace_back((const bcos::byte*)topicIt.data(), topicIt.size());
    }
    return bcos::protocol::LogEntry(bcos::bytes(_logEntry.address.begin(), _logEntry.address.end()),
        topics, bcos::bytes(_logEntry.data.begin(), _logEntry.data.end()));
}

inline bcos::protocol::TwoPCParams toBcosTwoPCParams(bcostars::TwoPCParams const& _param)
{
    bcos::protocol::TwoPCParams bcosTwoPCParams;
    bcosTwoPCParams.number = _param.blockNumber;
    bcosTwoPCParams.primaryTableName = _param.primaryTableName;
    bcosTwoPCParams.primaryTableKey = _param.primaryTableKey;
    bcosTwoPCParams.startTS = _param.startTS;
    return bcosTwoPCParams;
}

inline bcostars::TwoPCParams toTarsTwoPCParams(bcos::protocol::TwoPCParams _param)
{
    bcostars::TwoPCParams tarsTwoPCParams;
    tarsTwoPCParams.blockNumber = _param.number;
    tarsTwoPCParams.primaryTableName = _param.primaryTableName;
    tarsTwoPCParams.primaryTableKey = _param.primaryTableKey;
    tarsTwoPCParams.startTS = _param.startTS;
    return tarsTwoPCParams;
}
}  // namespace bcostars