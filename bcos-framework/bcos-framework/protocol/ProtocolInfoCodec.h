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
 * @brief interface for ProtocolInfoCodec
 * @file ProtocolInfoCodec.h
 * @author: yujiechen
 * @date 2022-03-22
 */
#pragma once
#include "bcos-framework/bcos-framework/protocol/ProtocolInfo.h"
#include <bcos-utilities/Common.h>
#include <memory>
namespace bcos
{
namespace protocol
{
class ProtocolInfoCodec
{
public:
    using ConstPtr = std::shared_ptr<ProtocolInfoCodec const>;
    using Ptr = std::shared_ptr<ProtocolInfoCodec>;
    ProtocolInfoCodec() = default;
    virtual ~ProtocolInfoCodec() {}

    virtual void encode(ProtocolInfo::ConstPtr _protocol, bcos::bytes& _encodeData) const = 0;
    virtual ProtocolInfo::Ptr decode(bcos::bytesConstRef _data) const = 0;
};
}  // namespace protocol
}  // namespace bcos