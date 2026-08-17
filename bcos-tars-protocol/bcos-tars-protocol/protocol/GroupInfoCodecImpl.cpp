/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "GroupInfoCodecImpl.h"
#include "bcos-tars-protocol/Common.h"

bcostars::protocol::GroupInfoCodecImpl::GroupInfoCodecImpl()
  : m_nodeFactory(std::make_shared<bcos::group::ChainNodeInfoFactory>()),
    m_groupFactory(std::make_shared<bcos::group::GroupInfoFactory>())
{}

bcostars::protocol::GroupInfoCodecImpl::~GroupInfoCodecImpl() = default;

bcos::group::GroupInfo::Ptr bcostars::protocol::GroupInfoCodecImpl::deserialize(
    const std::string& _encodedData)
{
    tars::TarsInputStream<tars::BufferReader> input;
    input.setBuffer((const char*)_encodedData.data(), _encodedData.size());

    bcostars::GroupInfo tarsGroupInfo;
    tarsGroupInfo.readFrom(input);
    return toBcosGroupInfo(m_nodeFactory, m_groupFactory, tarsGroupInfo);
}

Json::Value bcostars::protocol::GroupInfoCodecImpl::serialize(
    bcos::group::GroupInfo::Ptr _groupInfo)
{
    (void)_groupInfo;
    return {};
}

void bcostars::protocol::GroupInfoCodecImpl::serialize(
    std::string& _encodedData, bcos::group::GroupInfo::Ptr _groupInfo)
{
    auto tarsGroupInfo = toTarsGroupInfo(_groupInfo);
    tars::TarsOutputStream<tars::BufferWriterString> output;
    tarsGroupInfo.writeTo(output);
    output.swap(_encodedData);
}
