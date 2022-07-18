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
 * @file GroupInfoCodecImpl.h
 * @author: yujiechen
 * @date 2022-03-29
 */
#pragma once
#include "bcos-framework/multigroup/GroupInfoCodec.h"
#include "bcos-tars-protocol/Common.h"

namespace bcostars
{
namespace protocol
{
class GroupInfoCodecImpl : public bcos::group::GroupInfoCodec
{
public:
    GroupInfoCodecImpl()
      : m_nodeFactory(std::make_shared<bcos::group::ChainNodeInfoFactory>()),
        m_groupFactory(std::make_shared<bcos::group::GroupInfoFactory>())
    {}

    ~GroupInfoCodecImpl() override {}

    bcos::group::GroupInfo::Ptr deserialize(const std::string& _encodedData) override
    {
        tars::TarsInputStream<tars::BufferReader> input;
        input.setBuffer((const char*)_encodedData.data(), _encodedData.size());

        bcostars::GroupInfo tarsGroupInfo;
        tarsGroupInfo.readFrom(input);
        return toBcosGroupInfo(m_nodeFactory, m_groupFactory, tarsGroupInfo);
    }

    void serialize(std::string& _encodedData, bcos::group::GroupInfo::Ptr _groupInfo) override
    {
        auto tarsGroupInfo = toTarsGroupInfo(_groupInfo);
        tars::TarsOutputStream<tars::BufferWriterString> output;
        tarsGroupInfo.writeTo(output);
        output.swap(_encodedData);
    }

private:
    bcos::group::ChainNodeInfoFactory::Ptr m_nodeFactory;
    bcos::group::GroupInfoFactory::Ptr m_groupFactory;
};
}  // namespace protocol
}  // namespace bcostars