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
 * @file Result.h
 * @brief std::expected-based value returns for hot RLP ingress paths (devp2p frames,
 *        P2P transactions, eth_sendRawTransaction), where malformed input is routine.
 *        Cold paths keep throwing RlpDecodeException/RlpEncodeException (Exceptions.h).
 */
#pragma once

#include "Exceptions.h"
#include <bcos-utilities/Exceptions.h>
#include <expected>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace bcos::codec::rlp
{
// Value-return error. `code` carries the same int32 as errinfo_rlpErrorCode (a DecodingError
// or a module-specific code such as protocol::EthBlockHeaderError); kRlpGenericError marks
// failures that never carried an RLP code (a plain std::runtime_error from a higher-level
// helper such as the eth wire-message validators).
struct RlpError
{
    int32_t code{0};
    std::string message;
    friend bool operator==(RlpError const&, RlpError const&) = default;
};

inline constexpr int32_t kRlpGenericError{-1};

template <typename T>
using RlpResult = std::expected<T, RlpError>;

// Builds the failure half of an RlpResult from any error code (an unscoped enum such as
// DecodingError, an enum class such as protocol::EthBlockHeaderError, or a raw int32).
template <typename E>
auto rlpFail(E code, std::string_view message)
{
    return std::unexpected(
        RlpError{.code = static_cast<int32_t>(code), .message = std::string(message)});
}

// Boundary adapter: runs f and converts any thrown failure into an RlpError value, so
// callers at a hot ingress boundary branch on values instead of catching. The success path
// is zero-cost, but note this is an adapter, not a non-throwing core: when f rejects
// malformed input it still throws and unwinds once inside this wrapper before the failure
// becomes a value. Paths that must never unwind need a genuinely non-throwing
// implementation (tryDecodeHeader/tryDecode) rather than captureRlp.
template <typename F>
auto captureRlp(F&& f) -> RlpResult<std::invoke_result_t<F>>
{
    using T = std::invoke_result_t<F>;
    try
    {
        if constexpr (std::is_void_v<T>)
        {
            std::forward<F>(f)();
            return {};
        }
        else
        {
            return std::forward<F>(f)();
        }
    }
    catch (boost::exception const& e)
    {
        // RlpDecodeException/RlpEncodeException derive from bcos::Exception (a
        // boost::exception) — NOT from bcos::Error, so catch (bcos::Error const&) handlers
        // elsewhere do not see them; the code travels in errinfo_rlpErrorCode precisely
        // because errorCode() is unavailable.
        // BOOST_THROW_EXCEPTION(std::invalid_argument(...)) — the repo's default idiom —
        // lands here as a boost::wrapexcept with no errinfo_comment; fall back to what()
        // so the message is not silently blanked.
        auto const* asStd = dynamic_cast<std::exception const*>(&e);
        return rlpFail(rlpErrorCode(e, kRlpGenericError),
            rlpErrorMessage(e, asStd != nullptr ? asStd->what() : ""));
    }
    catch (std::exception const& e)
    {
        return std::unexpected(RlpError{.code = kRlpGenericError, .message = e.what()});
    }
}

// The inverse of captureRlp, for boundaries that keep the exception style (for example the
// once-per-connection handshake): the value on success, a std::runtime_error with
// _context + the error message on failure.
template <typename T>
T unwrapOrThrow(RlpResult<T>&& _result, std::string_view _context)
{
    if (!_result)
    {
        throw std::runtime_error(std::string(_context) + _result.error().message);
    }
    return std::move(*_result);
}
}  // namespace bcos::codec::rlp
