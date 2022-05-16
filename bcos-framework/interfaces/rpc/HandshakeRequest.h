/*
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
 * @file HandshakeRequest.h
 * @author: yujiechen
 * @date 2022-2-29
 */

#pragma once
#include <bcos-framework/interfaces/Common.h>
#include <bcos-framework/interfaces/protocol/ProtocolInfo.h>
#include <json/json.h>
#include <memory>
namespace bcos
{
namespace rpc
{
class HandshakeRequest
{
public:
    using Ptr = std::shared_ptr<HandshakeRequest>;
    HandshakeRequest() : m_protocol(std::make_shared<bcos::protocol::ProtocolInfo>()) {}

    HandshakeRequest(bcos::protocol::ProtocolInfo::ConstPtr _protocol)
    {
        m_protocol = std::const_pointer_cast<bcos::protocol::ProtocolInfo>(_protocol);
    }

    virtual std::shared_ptr<bcos::bytes> encode() const
    {
        Json::Value request;
        request["minVersion"] = m_protocol->minVersion();
        request["maxVersion"] = m_protocol->maxVersion();
        request["moduleID"] = m_protocol->protocolModuleID();
        Json::FastWriter fastWriter;
        auto requestStr = fastWriter.write(request);
        return std::make_shared<bcos::bytes>(requestStr.begin(), requestStr.end());
    }

    virtual bool decode(bcos::bytes const& _data)
    {
        std::string dataStr(_data.begin(), _data.end());
        try
        {
            Json::Reader reader;
            Json::Value request;
            if (!reader.parse(dataStr, request))
            {
                BCOS_LOG(WARNING) << LOG_DESC("HandshakeRequest: invalid json object")
                                  << LOG_KV("data", dataStr);
                return false;
            }
            // get the moduleID
            auto moduleID = request["moduleID"].asUInt();
            if (moduleID > (uint32_t)(bcos::protocol::ProtocolModuleID::MAX_PROTOCOL_MODULE))
            {
                BCOS_LOG(WARNING) << LOG_DESC("HandshakeRequest: invalid moduleID")
                                  << LOG_KV("moduleID", moduleID) << LOG_KV("data", dataStr);
                return false;
            }
            m_protocol->setProtocolModuleID((bcos::protocol::ProtocolModuleID)(moduleID));
            // set minVersion
            m_protocol->setMinVersion(request["minVersion"].asUInt());
            // set maxVersion
            m_protocol->setMaxVersion(request["maxVersion"].asUInt());
            BCOS_LOG(INFO) << LOG_DESC("HandshakeRequest")
                           << LOG_KV("module", m_protocol->protocolModuleID())
                           << LOG_KV("minVersion", m_protocol->minVersion())
                           << LOG_KV("maxVersion", m_protocol->maxVersion());
            return true;
        }
        catch (std::exception const& e)
        {
            BCOS_LOG(WARNING) << LOG_DESC("HandshakeRequest decode exception")
                              << LOG_KV("error", boost::diagnostic_information(e))
                              << LOG_KV("data", dataStr);
        }
        return false;
    }
    bcos::protocol::ProtocolInfo const& protocol() const { return *m_protocol; }

private:
    bcos::protocol::ProtocolInfo::Ptr m_protocol;
};
}  // namespace rpc
}  // namespace bcos