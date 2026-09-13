/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file RlpTake.h
 * @brief Non-throwing RLP item extraction helpers shared by the eth/ and rlpx
 *        wire decoders (single inline definition; previously duplicated per TU).
 * @date 2026/9/13
 */
#pragma once

#include "Try.h"
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/Result.h>
#include <bcos-utilities/Common.h>
#include <string>
#include <string_view>

namespace bcos::devp2p::detail
{
// kRlpGenericError with a fixed message: the wire-level validation failures that
// never carried an RLP code (formerly plain std::runtime_error texts).
inline bcos::codec::rlp::RlpError genericError(std::string_view _message)
{
    return {.code = bcos::codec::rlp::kRlpGenericError, .message = std::string(_message)};
}

// RLP-layer failures propagate the tryDecodeHeader error as-is; item-level
// validation failures report genericError with the given message.

// Expects a list at `_view` and returns a view over its payload.
inline bcos::codec::rlp::RlpResult<bcos::bytesRef> takeListPayload(
    bcos::bytesRef& _view, std::string_view _notListMessage)
{
    RLP_TRY(auto header, bcos::codec::rlp::tryDecodeHeader(_view));
    if (!header.isList)
    {
        return std::unexpected(genericError(_notListMessage));
    }
    bcos::bytesRef payload(_view.data(), header.payloadLength);
    _view = bcos::bytesRef(
        _view.data() + header.payloadLength, _view.size() - header.payloadLength);
    return payload;
}

inline bcos::codec::rlp::RlpResult<uint64_t> takeUint(
    bcos::bytesRef& _view, std::string_view _message)
{
    RLP_TRY(auto header, bcos::codec::rlp::tryDecodeHeader(_view));
    // Same integer rules as the throwing codec: no lists, nothing wider than
    // uint64, no non-canonical leading zero byte.
    if (header.isList || header.payloadLength > sizeof(uint64_t) ||
        (header.payloadLength >= 1 && _view[0] == 0))
    {
        return std::unexpected(genericError(_message));
    }
    uint64_t value = 0;
    for (size_t i = 0; i < header.payloadLength; ++i)
    {
        value = (value << 8) | _view[i];
    }
    _view = bcos::bytesRef(
        _view.data() + header.payloadLength, _view.size() - header.payloadLength);
    return value;
}

inline bcos::codec::rlp::RlpResult<bcos::bytes> takeBytes(
    bcos::bytesRef& _view, std::string_view _message)
{
    RLP_TRY(auto header, bcos::codec::rlp::tryDecodeHeader(_view));
    if (header.isList)
    {
        return std::unexpected(genericError(_message));
    }
    bcos::bytes out(_view.data(), _view.data() + header.payloadLength);
    _view = bcos::bytesRef(
        _view.data() + header.payloadLength, _view.size() - header.payloadLength);
    return out;
}

inline bcos::codec::rlp::RlpResult<std::string> takeString(
    bcos::bytesRef& _view, std::string_view _message)
{
    RLP_TRY(auto bytes, takeBytes(_view, _message));
    return std::string(bytes.begin(), bytes.end());
}
}  // namespace bcos::devp2p::detail
