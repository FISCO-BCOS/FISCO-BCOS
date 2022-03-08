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
#include <memory>
namespace bcos
{
namespace protocol
{
class ProtocolInfo
{
public:
    using Ptr = std::shared_ptr<ProtocolInfo>;
    using ConstPtr = std::shared_ptr<ProtocolInfo const>;
    ProtocolInfo() = default;
    ProtocolInfo(
        ProtocolModuleID _moduleID, ProtocolVersion _minVersion, ProtocolVersion _maxVersion)
      : m_protocolModuleID(_moduleID),
        m_minVersion(_minVersion),
        m_maxVersion(_maxVersion),
        m_version(m_minVersion)
    {}
    virtual ~ProtocolInfo() {}
    virtual void setProtocolModuleID(ProtocolModuleID _moduleID) { m_protocolModuleID = _moduleID; }
    virtual void setMinVersion(ProtocolVersion _minVersion) { m_minVersion = _minVersion; }
    virtual void setMaxVersion(ProtocolVersion _maxVersion) { m_maxVersion = _maxVersion; }
    // set the negotiated version
    virtual void setVersion(ProtocolVersion _version) { m_version = _version; }

    // the moduleID
    virtual ProtocolModuleID protocolModuleID() const { return m_protocolModuleID; }
    // the minimum supported version number
    virtual ProtocolVersion minVersion() const { return m_minVersion; }
    // the maximum supported version number
    virtual ProtocolVersion maxVersion() const { return m_maxVersion; }

    // the negotiated version
    virtual ProtocolVersion version() const { return m_version; }

protected:
    ProtocolModuleID m_protocolModuleID;
    ProtocolVersion m_minVersion;
    ProtocolVersion m_maxVersion;
    ProtocolVersion m_version;
};
}  // namespace protocol
}  // namespace bcos
