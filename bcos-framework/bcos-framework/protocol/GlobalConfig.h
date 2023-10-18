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
 * @brief interface for ProtocolInfo
 * @file ProtocolInfo.h
 * @author: yujiechen
 * @date 2022-03-08
 */
#pragma once
#include "Protocol.h"
#include "ProtocolInfo.h"
#include "ProtocolInfoCodec.h"
#include <map>
#include <memory>

namespace bcos
{
namespace protocol
{
class GlobalConfig
{
public:
    static GlobalConfig& instance()
    {
        static GlobalConfig ins;
        return ins;
    }
    GlobalConfig()
    {
        // nodeService
        c_supportedProtocols.insert({ProtocolModuleID::NodeService,
            std::make_shared<ProtocolInfo>(
                ProtocolModuleID::NodeService, ProtocolVersion::V0, ProtocolVersion::V2)});
        // gatewayService
        c_supportedProtocols.insert({ProtocolModuleID::GatewayService,
            std::make_shared<ProtocolInfo>(
                ProtocolModuleID::GatewayService, ProtocolVersion::V0, ProtocolVersion::V2)});
        // rpcService && SDK
        c_supportedProtocols.insert({ProtocolModuleID::RpcService,
            std::make_shared<ProtocolInfo>(
                ProtocolModuleID::RpcService, ProtocolVersion::V0, ProtocolVersion::V1)});
        // executorService
        c_supportedProtocols.insert({ProtocolModuleID::ExecutorService,
            std::make_shared<ProtocolInfo>(
                ProtocolModuleID::ExecutorService, ProtocolVersion::V0, ProtocolVersion::V1)});
    }
    virtual ~GlobalConfig() {}

    ProtocolInfo::ConstPtr protocolInfo(ProtocolModuleID _moduleID) const
    {
        auto it = c_supportedProtocols.find(_moduleID);
        if (it == c_supportedProtocols.end())
        {
            return nullptr;
        }
        return it->second;
    }

    std::map<ProtocolModuleID, ProtocolInfo::Ptr> const& supportedProtocols() const
    {
        return c_supportedProtocols;
    }
    // Note: must set the protocolInfo codec when init
    virtual void setCodec(ProtocolInfoCodec::Ptr _codec) { m_codec = _codec; }
    virtual ProtocolInfoCodec::Ptr codec() const { return m_codec; }

    virtual void setIsWasm(bool _isWasm) noexcept { m_isWasm = _isWasm; }
    virtual bool isWasm() const noexcept { return m_isWasm; }

    void setStorageType(std::string const& _storageType) { m_storageType = _storageType; }
    std::string const& storageType() const { return m_storageType; }

    void setEnableDAG(bool _enableDAG) { m_enableDAG = _enableDAG; }
    bool enableDAG() const { return m_enableDAG; }

    void setNeedRetInput(bool _needRetInput) { m_needRetInput = _needRetInput; }
    bool needRetInput() const { return m_needRetInput; }

    BlockVersion minSupportedVersion() const { return m_minSupportedVersion; }
    BlockVersion maxSupportedVersion() const { return m_maxSupportedVersion; }

    std::mutex& signalMutex() const { return x_signalMutex; }

private:
    std::map<ProtocolModuleID, ProtocolInfo::Ptr> c_supportedProtocols;
    // the minimum supported version
    BlockVersion m_minSupportedVersion = BlockVersion::MIN_VERSION;
    BlockVersion m_maxSupportedVersion = BlockVersion::MAX_VERSION;
    ProtocolInfoCodec::Ptr m_codec;
    std::string m_storageType;
    bool m_enableDAG = true;
    bool m_needRetInput = false;  // need add 'input' param in sendTransaction() return value
    bool m_isWasm = false;
    mutable std::mutex x_signalMutex;
};
}  // namespace protocol
}  // namespace bcos
#define g_BCOSConfig bcos::protocol::GlobalConfig::instance()