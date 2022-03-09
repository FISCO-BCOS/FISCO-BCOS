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
 * @file GatewayNodeStatus.cpp
 * @author: yujiechen
 * @date 2021-12-31
 */
#include "GatewayNodeStatus.h"
#include <bcos-tars-protocol/Common.h>
#include <bcos-tars-protocol/protocol/GroupNodeInfoImpl.h>
using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::gateway;

GatewayNodeStatus::GatewayNodeStatus()
  : m_tarsStatus(std::make_shared<bcostars::GatewayNodeStatus>())
{}

bytesPointer GatewayNodeStatus::encode()
{
    // append groupInfos to m_tarsStatus
    m_tarsStatus->nodeList.clear();
    for (auto const& it : m_groupNodeInfos)
    {
        auto groupNodeInfoImpl =
            std::dynamic_pointer_cast<bcostars::protocol::GroupNodeInfoImpl>(it);
        m_tarsStatus->nodeList.emplace_back(groupNodeInfoImpl->inner());
    }
    auto encodeData = std::make_shared<bytes>();
    tars::TarsOutputStream<bcostars::protocol::BufferWriterByteVector> output;
    m_tarsStatus->writeTo(output);
    output.getByteBuffer().swap(*encodeData);
    return encodeData;
}

void GatewayNodeStatus::decode(bytesConstRef _data)
{
    tars::TarsInputStream<tars::BufferReader> input;
    input.setBuffer((const char*)_data.data(), _data.size());
    m_tarsStatus->readFrom(input);
    // decode into m_groupNodeInfos
    m_groupNodeInfos.clear();
    for (auto& it : m_tarsStatus->nodeList)
    {
        auto groupNodeInfo = std::make_shared<bcostars::protocol::GroupNodeInfoImpl>(
            [m_groupNodeInfo = it]() mutable { return &m_groupNodeInfo; });
        m_groupNodeInfos.emplace_back(groupNodeInfo);
    }
}