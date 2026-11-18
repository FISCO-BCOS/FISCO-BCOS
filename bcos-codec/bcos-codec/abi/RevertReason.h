// Copyright (C) 2026 FISCO BCOS. SPDX-License-Identifier: Apache-2.0
#pragma once

#include <bcos-utilities/DataConvertUtility.h>
#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace bcos::codec::abi
{
/// The human-readable form of the ABI-encoded revert payload the way geth's abi.UnpackRevert
/// reads it: Error(string) → the string, Panic(uint256) → the Solidity panic description.
/// nullopt for raw / custom-error / malformed data (the caller then decodes `data` itself).
inline std::optional<std::string> decodeRevertReason(bytesConstRef output)
{
    constexpr size_t c_selectorSize = 4;
    constexpr size_t c_wordSize = 32;
    constexpr std::array<uint8_t, 4> c_errorSelector{0x08, 0xc3, 0x79, 0xa0};  // Error(string)
    constexpr std::array<uint8_t, 4> c_panicSelector{0x4e, 0x48, 0x7b, 0x71};  // Panic(uint256)
    if (output.size() < c_selectorSize + c_wordSize)
    {
        return std::nullopt;
    }
    auto const selector = output.getCroppedData(0, c_selectorSize);
    if (std::equal(selector.begin(), selector.end(), c_panicSelector.begin()))
    {
        auto const code = fromBigEndian<u256>(output.getCroppedData(c_selectorSize, c_wordSize));
        auto panicCode = [](u256 value) {
            static constexpr std::string_view c_hexDigits = "0123456789abcdef";
            std::string digits;
            do
            {
                digits.push_back(c_hexDigits[static_cast<size_t>(value & 0xf)]);
                value >>= 4;
            } while (value != 0);
            std::reverse(digits.begin(), digits.end());
            return "0x" + digits;
        };
        if (code > 0xff)
        {
            return "unknown panic code: " + panicCode(code);
        }
        switch (static_cast<uint64_t>(code))
        {
        case 0x00:
            return "generic panic";
        case 0x01:
            return "assert(false)";
        case 0x11:
            return "arithmetic underflow or overflow";
        case 0x12:
            return "division or modulo by zero";
        case 0x21:
            return "enum overflow";
        case 0x22:
            return "invalid encoded storage byte array accessed";
        case 0x31:
            return "out-of-bounds array access; popping on an empty array";
        case 0x32:
            return "out-of-bounds access of an array or bytesN";
        case 0x41:
            return "out of memory";
        case 0x51:
            return "uninitialized function";
        default:
            return "unknown panic code: " + panicCode(code);
        }
    }
    if (!std::equal(selector.begin(), selector.end(), c_errorSelector.begin()))
    {
        return std::nullopt;
    }
    // Error(string): selector | offset | ... | length | bytes. Like abi.UnpackRevert,
    // accept an in-bounds dynamic offset, including a non-canonical offset other than 32.
    auto const offset = fromBigEndian<u256>(output.getCroppedData(c_selectorSize, c_wordSize));
    if (offset > output.size() - c_selectorSize - c_wordSize)
    {
        return std::nullopt;
    }
    auto const lengthStart = c_selectorSize + static_cast<size_t>(offset);
    auto const length = fromBigEndian<u256>(output.getCroppedData(lengthStart, c_wordSize));
    auto const dataStart = lengthStart + c_wordSize;
    if (length > output.size() - dataStart)
    {
        return std::nullopt;
    }
    auto const reason = output.getCroppedData(dataStart, static_cast<size_t>(length));
    return std::string(reason.begin(), reason.end());
}

}  // namespace bcos::codec::abi
