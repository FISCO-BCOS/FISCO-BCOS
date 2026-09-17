// Copyright (C) 2026 FISCO BCOS. SPDX-License-Identifier: Apache-2.0
// @file RlpTake.h
// @brief Non-throwing RLP item extraction helpers shared by the eth/ and rlpx
//        wire decoders (single inline definition; previously duplicated per TU).
#pragma once

#include "Try.h"
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/Result.h>
#include <bcos-utilities/Common.h>
#include <string>
#include <string_view>

namespace bcos::devp2p::detail
{
// c_rlpGenericError with a fixed message: the wire-level validation failures that
// never carried an RLP code (formerly plain std::runtime_error texts).
inline bcos::codec::rlp::RlpError genericError(std::string_view _message)
{
    return {.code = bcos::codec::rlp::c_rlpGenericError, .message = std::string(_message)};
}

// RLP-layer and item-level failures both propagate the codec's tryDecodeHeader/tryDecode
// error as-is: the canonical rules (no list, width, no leading zero byte, exact fixed
// size) live in RLPDecode.h as the single source of truth. genericError is only for
// wire-level validations the codec does not cover (e.g. "expected a list").

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
    _view =
        bcos::bytesRef(_view.data() + header.payloadLength, _view.size() - header.payloadLength);
    return payload;
}

// One item extractor for every type with a non-throwing tryDecode overload
// (uint64_t, bcos::bytes, std::string, h256); advances `_view` past the item.
template <typename T>
inline bcos::codec::rlp::RlpResult<T> take(bcos::bytesRef& _view)
{
    T out{};
    if (auto result = bcos::codec::rlp::tryDecode(_view, out); !result) [[unlikely]]
    {
        return std::unexpected(result.error());
    }
    return out;
}
}  // namespace bcos::devp2p::detail
