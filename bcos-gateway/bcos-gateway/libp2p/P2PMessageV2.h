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
 * @file P2PMessageV2.h
 * @brief: extend srcNodeID and dstNodeID for message forward
 * @author: yujiechen
 * @date 2021-05-04
 */
#pragma once
#include "P2PMessage.h"

namespace bcos
{
namespace gateway
{
class P2PMessageV2 : public P2PMessage
{
public:
    using Ptr = std::shared_ptr<P2PMessageV2>;
    P2PMessageV2() : P2PMessage() {}
    ~P2PMessageV2() override {}

protected:
    ssize_t decodeHeader(bytesConstRef _buffer) override;
    bool encodeHeader(bytes& _buffer) override;
};

class P2PMessageFactoryV2 : public P2PMessageFactory
{
public:
    using Ptr = std::shared_ptr<P2PMessageFactoryV2>;
    P2PMessageFactoryV2() = default;
    ~P2PMessageFactoryV2() override {}
    bcos::boostssl::MessageFace::Ptr buildMessage() override
    {
        auto message = std::make_shared<P2PMessageV2>();
        return message;
    }
};
}  // namespace gateway
}  // namespace bcos