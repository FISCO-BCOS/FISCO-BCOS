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
 * @brief std::expected-based value returns for the hot RLP ingress paths
 *        (devp2p frames, P2P transactions, eth_sendRawTransaction), where
 *        malformed input is routine and its rate is attacker-controlled.
 *        Cold paths (local storage, encoding, hashing) keep throwing
 *        RlpDecodeException/RlpEncodeException (see Exceptions.h).
 */
#pragma once

#include "Exceptions.h"
#include <bcos-utilities/Exceptions.h>
#include <expected>
#include <string>
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

// Boundary adapter: runs f and converts any thrown failure into an RlpError value. The
// success path is zero-cost; use it where an exception-based implementation feeds a hot
// ingress boundary whose callers should branch on values instead of catching.
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
        // RlpDecodeException/RlpEncodeException derive from bcos::Error (a boost::exception).
        // Default-construct then set the one field rather than writing {.code = ...}: a partial
        // designated initialiser trips GCC's -Wmissing-field-initializers (-Werror) at every
        // instantiation of this template (same rule as Eip7702Recover.h's Account init).
        RlpError error;
        error.code = kRlpGenericError;
        if (auto const* code = boost::get_error_info<errinfo_rlpErrorCode>(e))
        {
            error.code = *code;
        }
        if (auto const* msg = boost::get_error_info<bcos::errinfo_comment>(e))
        {
            error.message = *msg;
        }
        return std::unexpected(std::move(error));
    }
    catch (std::exception const& e)
    {
        return std::unexpected(RlpError{.code = kRlpGenericError, .message = e.what()});
    }
}
}  // namespace bcos::codec::rlp
