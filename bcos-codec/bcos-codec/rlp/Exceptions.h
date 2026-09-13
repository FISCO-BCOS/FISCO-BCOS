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
 * @file Exceptions.h
 * @brief Exception types thrown by the RLP codec (decode/encode failures)
 */
#pragma once

#include <bcos-utilities/Exceptions.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace bcos::codec::rlp
{
DERIVE_BCOS_EXCEPTION(RlpDecodeException);
DERIVE_BCOS_EXCEPTION(RlpEncodeException);

// Carries the former DecodingError (or a module-specific code such as
// protocol::EthBlockHeaderError) as an int32, so catchers can keep asserting on error codes:
//   boost::get_error_info<errinfo_rlpErrorCode>(e)
using errinfo_rlpErrorCode = boost::error_info<struct tag_rlpErrorCode, int32_t>;

[[noreturn]] inline void throwRlpDecodeError(int32_t code, std::string_view message)
{
    BOOST_THROW_EXCEPTION(RlpDecodeException{}
                          << errinfo_rlpErrorCode(code)
                          << bcos::errinfo_comment(std::string(message)));
}

[[noreturn]] inline void throwRlpEncodeError(int32_t code, std::string_view message)
{
    BOOST_THROW_EXCEPTION(RlpEncodeException{}
                          << errinfo_rlpErrorCode(code)
                          << bcos::errinfo_comment(std::string(message)));
}

// Enum convenience overloads: accept DecodingError, protocol::EthBlockHeaderError, etc.
template <typename E>
    requires std::is_enum_v<E>
[[noreturn]] inline void throwRlpDecodeError(E code, std::string_view message)
{
    throwRlpDecodeError(static_cast<int32_t>(code), message);
}

template <typename E>
    requires std::is_enum_v<E>
[[noreturn]] inline void throwRlpEncodeError(E code, std::string_view message)
{
    throwRlpEncodeError(static_cast<int32_t>(code), message);
}
}  // namespace bcos::codec::rlp
