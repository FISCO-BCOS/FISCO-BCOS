// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2026 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0
#pragma once

/// @file
/// RLP prefix encoding constants (Yellow Paper Appendix B), shared by the encoder and the decoder.

#include <cstddef>
#include <cstdint>

namespace evmone::rlp
{
/// The largest payload encoded in the short form; longer payloads use the long form.
constexpr size_t SHORT_LENGTH_LIMIT = 55;
/// Base of a byte-string prefix: a short string is this byte plus its length (0x80..0xb7).
constexpr uint8_t SHORT_STRING_BASE = 0x80;
/// Base of a list prefix: a short list is this byte plus its payload length (0xc0..0xf7).
constexpr uint8_t SHORT_LIST_BASE = 0xc0;
}  // namespace evmone::rlp
