#pragma once

#include <cstddef>
#include <cstdint>

namespace bcos::executor
{

/// EIP-7702 delegation designator prefix (`0xEF0100`).
constexpr uint8_t EIP7702_DELEGATION_PREFIX[3] = {0xEF, 0x01, 0x00};
constexpr size_t EIP7702_DELEGATION_CODE_SIZE = 23;
constexpr size_t EIP7702_DELEGATION_TARGET_OFFSET = sizeof(EIP7702_DELEGATION_PREFIX);

/// Defensive upper bound on `authorization_list` length (gas also limits practical size).
constexpr size_t EIP7702_MAX_AUTHORIZATION_LIST_SIZE = 256;

}  // namespace bcos::executor
