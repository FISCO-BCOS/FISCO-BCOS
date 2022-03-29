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
 * @brief implement for ProtocolInfoCodec
 * @file ProtocolInfoCodecImpl.h
 * @author: yujiechen
 * @date 2022-03-22
 */
#include "ProtocolInfoCodecImpl.h"
#include <bcos-tars-protocol/Common.h>

using namespace bcostars;
using namespace bcostars::protocol;

void ProtocolInfoCodecImpl::encode(
    bcos::protocol::ProtocolInfo::ConstPtr _protocol, bcos::bytes& _encodeData) const
{
    bcostars::ProtocolInfo tarsProtcolInfo;
    tarsProtcolInfo.moduleID = _protocol->protocolModuleID();
    tarsProtcolInfo.minVersion = _protocol->minVersion();
    tarsProtcolInfo.maxVersion = _protocol->maxVersion();
    tars::TarsOutputStream<bcostars::protocol::BufferWriterByteVector> output;
    tarsProtcolInfo.writeTo(output);
    output.getByteBuffer().swap(_encodeData);
}

bcos::protocol::ProtocolInfo::Ptr ProtocolInfoCodecImpl::decode(bcos::bytesConstRef _data) const
{
    tars::TarsInputStream<tars::BufferReader> input;
    input.setBuffer((const char*)_data.data(), _data.size());
    bcostars::ProtocolInfo tarsProtcolInfo;
    tarsProtcolInfo.readFrom(input);

    auto protocolInfo = std::make_shared<bcos::protocol::ProtocolInfo>();
    protocolInfo->setProtocolModuleID((bcos::protocol::ProtocolModuleID)tarsProtcolInfo.moduleID);
    protocolInfo->setMinVersion(tarsProtcolInfo.minVersion);
    protocolInfo->setMaxVersion(tarsProtcolInfo.maxVersion);
    return protocolInfo;
}