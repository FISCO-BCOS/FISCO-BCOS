// Copyright (C) 2026 FISCO BCOS. SPDX-License-Identifier: Apache-2.0
// @file Exceptions.h
// @brief Exception types thrown by the RLP codec (decode/encode failures)
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

// Accepts any error code: an unscoped enum (DecodingError), an enum class
// (protocol::EthBlockHeaderError, ...) or a raw int32.
template <typename E>
concept RlpErrorCode = std::is_enum_v<E> || std::is_same_v<E, int32_t>;

template <RlpErrorCode E>
[[noreturn]] inline void throwRlpDecodeError(E code, std::string_view message)
{
    BOOST_THROW_EXCEPTION(RlpDecodeException{} << errinfo_rlpErrorCode(static_cast<int32_t>(code))
                                               << bcos::errinfo_comment(std::string(message)));
}

template <RlpErrorCode E>
[[noreturn]] inline void throwRlpEncodeError(E code, std::string_view message)
{
    BOOST_THROW_EXCEPTION(RlpEncodeException{} << errinfo_rlpErrorCode(static_cast<int32_t>(code))
                                               << bcos::errinfo_comment(std::string(message)));
}

// Shared accessors for the two error_info fields, so catch sites stop re-deriving
// boost::get_error_info with per-site fallback strings. The fallback is returned when the
// exception carries no such info (e.g. a foreign boost::exception).
inline std::string rlpErrorMessage(boost::exception const& e, std::string_view fallback)
{
    if (auto const* msg = boost::get_error_info<bcos::errinfo_comment>(e))
    {
        return *msg;
    }
    return std::string(fallback);
}

inline int32_t rlpErrorCode(boost::exception const& e, int32_t fallback)
{
    if (auto const* code = boost::get_error_info<errinfo_rlpErrorCode>(e))
    {
        return *code;
    }
    return fallback;
}
}  // namespace bcos::codec::rlp
