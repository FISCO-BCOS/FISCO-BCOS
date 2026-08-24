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
 * @file FrontMessage.h
 * @author: octopus
 * @date 2021-04-20
 */

#pragma once

#include <bcos-utilities/Common.h>


namespace bcos::front
{
enum MessageDecodeStatus
{
    MESSAGE_ERROR = -1,
    MESSAGE_COMPLETE = 0,
    MESSAGE_INCOMPLETE = 1
};

// FIB-69: cap the payload size accepted from the gateway as a second line of defense.
// MUST stay equal to gateway MAX_MESSAGE_LENGTH (bcos-gateway/bcos-gateway/Common.h).
// Honest senders never trip this check: Session::asyncSendMessage rejects messages whose
// UNCOMPRESSED length exceeds the same 32 MB bound before encoding, so with equal caps a
// message a peer could legitimately send is always accepted here. The receiver-side
// P2PMessage::decode() bound only covers the compressed wire bytes (compression is on by
// default), so this constant is the receiver's only limit on the decompressed payload —
// which is why the check stays. A smaller cap here silently drops legitimate large
// messages (e.g. consensus proposals of large blocks) that pass the gateway.
constexpr std::size_t MAX_PAYLOAD_LENGTH = 32UL * 1024 * 1024;

/// moduleID          :2 bytes
/// UUID length       :1 bytes
/// UUID              :UUID length bytes
/// ext               :2 bytes
/// payload
class FrontMessage
{
public:
    using Ptr = std::shared_ptr<FrontMessage>;

    /// moduleID(2) + UUID length(1) + ext(2)
    const static size_t HEADER_MIN_LENGTH = 5;
    /// The maximum front uuid length  10M
    const static size_t MAX_MESSAGE_UUID_SIZE = 255;

    enum ExtFlag
    {
        Response = 0x0001,
    };

    FrontMessage() = default;
    virtual ~FrontMessage() = default;

    virtual uint16_t moduleID();
    virtual void setModuleID(uint16_t _moduleID);

    virtual uint16_t ext();
    virtual void setExt(uint16_t _ext);

    virtual bytesConstRef uuid();
    virtual void setUuid(bytes _uuid);

    virtual bytesConstRef payload();
    virtual void setPayload(bytesConstRef _payload);

    virtual void setResponse();
    virtual bool isResponse();

    bool encodeHeader(bytes& buffer);
    virtual bool encode(bytes& _buffer);
    virtual ssize_t decode(bytesConstRef _buffer);

    static uint16_t tryDecodeModuleID(bytesConstRef _buffer);

private:
    bytes m_uuid;
    bytesConstRef m_payload;
    uint16_t m_moduleID = 0;
    uint16_t m_ext = 0;
};

class FrontMessageFactory
{
public:
    using Ptr = std::shared_ptr<FrontMessageFactory>;

    virtual ~FrontMessageFactory();

    virtual FrontMessage::Ptr buildMessage();
};

}  // namespace bcos::front
