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

namespace bcos
{
namespace front
{
enum MessageDecodeStatus
{
    MESSAGE_ERROR = -1,
    MESSAGE_COMPLETE = 0,
    MESSAGE_INCOMPLETE = 1
};

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

public:
    FrontMessage()
    {
        m_uuid = std::make_shared<bytes>();
        m_payload = bytesConstRef();
    }

    virtual ~FrontMessage() {}

public:
    virtual uint16_t moduleID() { return m_moduleID; }
    virtual void setModuleID(uint16_t _moduleID) { m_moduleID = _moduleID; }

    virtual uint16_t ext() { return m_ext; }
    virtual void setExt(uint16_t _ext) { m_ext = _ext; }

    virtual std::shared_ptr<bytes> uuid() { return m_uuid; }
    virtual void setUuid(std::shared_ptr<bytes> _uuid) { m_uuid = _uuid; }

    virtual bytesConstRef payload() { return m_payload; }
    virtual void setPayload(bytesConstRef _payload) { m_payload = _payload; }

    virtual void setResponse() { m_ext |= ExtFlag::Response; }
    virtual bool isResponse() { return m_ext & ExtFlag::Response; }

public:
    virtual bool encode(bytes& _buffer);
    virtual ssize_t decode(bytesConstRef _buffer);

protected:
    uint16_t m_moduleID = 0;
    std::shared_ptr<bytes> m_uuid;
    uint16_t m_ext = 0;
    bytesConstRef m_payload;  ///< message data
};

class FrontMessageFactory
{
public:
    using Ptr = std::shared_ptr<FrontMessageFactory>;

    virtual ~FrontMessageFactory() {}

    virtual FrontMessage::Ptr buildMessage()
    {
        auto message = std::make_shared<FrontMessage>();
        return message;
    }
};

}  // namespace front
}  // namespace bcos
