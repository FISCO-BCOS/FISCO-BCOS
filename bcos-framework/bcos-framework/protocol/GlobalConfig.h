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
#include <utility>

namespace bcos::protocol
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
        m_supportedProtocols.insert({ProtocolModuleID::NodeService,
            std::make_shared<ProtocolInfo>(
                ProtocolModuleID::NodeService, ProtocolVersion::V0, ProtocolVersion::V1)});
        // gatewayService
        m_supportedProtocols.insert({ProtocolModuleID::GatewayService,
            std::make_shared<ProtocolInfo>(
                ProtocolModuleID::GatewayService, ProtocolVersion::V0, ProtocolVersion::V2)});
        // rpcService && SDK
        m_supportedProtocols.insert({ProtocolModuleID::RpcService,
            std::make_shared<ProtocolInfo>(
                ProtocolModuleID::RpcService, ProtocolVersion::V0, ProtocolVersion::V1)});
        // executorService
        m_supportedProtocols.insert({ProtocolModuleID::ExecutorService,
            std::make_shared<ProtocolInfo>(
                ProtocolModuleID::ExecutorService, ProtocolVersion::V0, ProtocolVersion::V1)});
    }
    virtual ~GlobalConfig() = default;

    ProtocolInfo::ConstPtr protocolInfo(ProtocolModuleID moduleID) const
    {
        auto it = m_supportedProtocols.find(moduleID);
        if (it == m_supportedProtocols.end())
        {
            return nullptr;
        }
        return it->second;
    }

    std::map<ProtocolModuleID, ProtocolInfo::Ptr> const& supportedProtocols() const
    {
        return m_supportedProtocols;
    }
    // Note: must set the protocolInfo codec when init
    virtual void setCodec(ProtocolInfoCodec::Ptr _codec) { m_codec = std::move(_codec); }
    virtual ProtocolInfoCodec::Ptr codec() const { return m_codec; }

    BlockVersion minSupportedVersion() const { return m_minSupportedVersion; }
    BlockVersion maxSupportedVersion() const { return m_maxSupportedVersion; }

private:
    std::map<ProtocolModuleID, ProtocolInfo::Ptr> m_supportedProtocols;
    // the minimum supported version
    BlockVersion m_minSupportedVersion = BlockVersion::MIN_VERSION;
    BlockVersion m_maxSupportedVersion = BlockVersion::MAX_VERSION;
    ProtocolInfoCodec::Ptr m_codec;
};
}  // namespace bcos::protocol
#define g_BCOSConfig bcos::protocol::GlobalConfig::instance()