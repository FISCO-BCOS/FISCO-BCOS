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
#include "bcos-framework/protocol/ProtocolInfo.h"
#include "bcos-utilities/Common.h"
#include <fmt/format.h>
#include <json/json.h>
#include <memory>

namespace bcos::rpc
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

    bcos::bytes encode() const
    {
        // Format JSON directly into bytes via fmt::format_to, avoiding
        // intermediate std::string.
        // Format: {"minVersion":X,"maxVersion":Y,"moduleID":Z}
        bcos::bytes result;
        fmt::format_to(std::back_inserter(result),
            R"({{"minVersion":{},"maxVersion":{},"moduleID":{}}})", m_protocol->minVersion(),
            m_protocol->maxVersion(), static_cast<unsigned>(m_protocol->protocolModuleID()));
        return result;
    }

    bool decode(bcos::bytesConstRef _data)
    {
        auto const* begin = (const char*)_data.data();
        auto const* end = begin + _data.size();
        try
        {
            Json::Reader reader;
            Json::Value request;
            if (!reader.parse(begin, end, request))
            {
                BCOS_LOG(WARNING) << LOG_DESC("HandshakeRequest: invalid json object")
                                  << LOG_KV("data", std::string_view(begin, _data.size()));
                return false;
            }
            // get the moduleID
            auto moduleID = request["moduleID"].asUInt();
            if (moduleID > (uint32_t)(bcos::protocol::ProtocolModuleID::MAX_PROTOCOL_MODULE))
            {
                BCOS_LOG(WARNING) << LOG_DESC("HandshakeRequest: invalid moduleID")
                                  << LOG_KV("moduleID", moduleID)
                                  << LOG_KV("data", std::string_view(begin, _data.size()));
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
                              << LOG_KV("message", boost::diagnostic_information(e))
                              << LOG_KV("data", std::string_view(begin, _data.size()));
        }
        return false;
    }
    bcos::protocol::ProtocolInfo const& protocol() const { return *m_protocol; }

private:
    bcos::protocol::ProtocolInfo::Ptr m_protocol;
};
}  // namespace bcos::rpc
