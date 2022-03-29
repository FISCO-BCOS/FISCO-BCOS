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
                ProtocolModuleID::NodeService, ProtocolVersion::V1, ProtocolVersion::V1)});
        // gatewayService
        c_supportedProtocols.insert({ProtocolModuleID::GatewayService,
            std::make_shared<ProtocolInfo>(
                ProtocolModuleID::GatewayService, ProtocolVersion::V1, ProtocolVersion::V1)});
        // rpcService && SDK
        c_supportedProtocols.insert({ProtocolModuleID::RpcService,
            std::make_shared<ProtocolInfo>(
                ProtocolModuleID::RpcService, ProtocolVersion::V1, ProtocolVersion::V1)});
        // executorService
        c_supportedProtocols.insert({ProtocolModuleID::ExecutorService,
            std::make_shared<ProtocolInfo>(
                ProtocolModuleID::ExecutorService, ProtocolVersion::V1, ProtocolVersion::V1)});
    }
    virtual ~GlobalConfig() {}

    ProtocolVersion defaultVersion() const { return m_defaultVersion; }
    ProtocolInfo::ConstPtr protocolInfo(ProtocolModuleID _moduleID) const
    {
        if (!c_supportedProtocols.count(_moduleID))
        {
            return nullptr;
        }
        return c_supportedProtocols.at(_moduleID);
    }

    std::map<ProtocolModuleID, ProtocolInfo::Ptr> const& supportedProtocols() const
    {
        return c_supportedProtocols;
    }

    Version version() const
    {
        ReadGuard l(x_version);
        return m_version;
    }

    void setVersion(Version _version)
    {
        UpgradableGuard l(x_version);
        if (m_version == _version)
        {
            return;
        }
        UpgradeGuard ul(l);
        m_version = _version;
    }

    // Note: must set the protocolInfo codec when init
    virtual void setCodec(ProtocolInfoCodec::Ptr _codec) { m_codec = _codec; }
    virtual ProtocolInfoCodec::Ptr codec() const { return m_codec; }

    Version minSupportedVersion() const { return m_minSupportedVersion; }
    Version maxSupportedVersion() const { return m_maxSupportedVersion; }

private:
    std::map<ProtocolModuleID, ProtocolInfo::Ptr> c_supportedProtocols;
    // default version before protocol-negotiate success
    ProtocolVersion m_defaultVersion = ProtocolVersion::V1;
    // the system version, can only be upgraded manually
    Version m_version = Version::RC3_VERSION;
    // the minimum supported version
    Version m_minSupportedVersion = Version::MIN_VERSION;
    Version m_maxSupportedVersion = Version::MAX_VERSION;

    ProtocolInfoCodec::Ptr m_codec;
    mutable bcos::SharedMutex x_version;
};
}  // namespace protocol
}  // namespace bcos
#define g_BCOSConfig bcos::protocol::GlobalConfig::instance()