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
 * @file HandshakeResponse.h
 * @author: octopus
 * @date 2021-10-26
 */

#pragma once
#include <bcos-framework/multigroup/GroupInfoCodec.h>
#include <json/json.h>
#include <algorithm>
#include <unordered_map>


namespace bcos
{
namespace cppsdk
{
namespace service
{
class HandshakeResponse
{
public:
    using Ptr = std::shared_ptr<HandshakeResponse>;
    using ConstPtr = std::shared_ptr<const HandshakeResponse>;
    HandshakeResponse(bcos::group::GroupInfoCodec::Ptr _groupInfoCodec)
      : m_groupInfoCodec(_groupInfoCodec)
    {}
    virtual ~HandshakeResponse() {}

    virtual bool decode(std::string const& _data);
    virtual void encode(std::string& _encodedData) const;

    int protocolVersion() const { return m_protocolVersion; }
    const std::vector<bcos::group::GroupInfo::Ptr>& groupInfoList() const
    {
        return m_groupInfoList;
    }
    const std::unordered_map<std::string, int64_t>& groupBlockNumber() const
    {
        return m_groupBlockNumber;
    }

    void setProtocolVersion(int _protocolVersion) { m_protocolVersion = _protocolVersion; }
    void setGroupInfoList(const std::vector<bcos::group::GroupInfo::Ptr>& _groupInfoList)
    {
        m_groupInfoList = _groupInfoList;
    }

private:
    std::vector<bcos::group::GroupInfo::Ptr> m_groupInfoList;
    std::unordered_map<std::string, int64_t> m_groupBlockNumber;
    bcos::group::GroupInfoCodec::Ptr m_groupInfoCodec;
    // Note: the nodes determine the protocol version
    uint32_t m_protocolVersion;
    bcos::protocol::ProtocolInfo::Ptr m_localProtocol;
};

}  // namespace service
}  // namespace cppsdk
}  // namespace bcos