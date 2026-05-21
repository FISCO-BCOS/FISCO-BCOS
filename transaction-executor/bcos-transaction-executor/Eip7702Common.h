#pragma once

#include "bcos-executor/src/Eip7702Delegation.h"
#include <cstddef>
#include <cstdint>

namespace bcos::executor_v1
{

/// EIP-2718 type byte for EIP-7702 (must match `bcos::rpc::TransactionType::EIP7702`).
constexpr uint8_t EIP_7702_WEB3_TX_TYPE = 4;

constexpr int64_t EIP_7702_PER_EMPTY_ACCOUNT_COST = 25000;
constexpr int64_t EIP_7702_PER_AUTH_BASE_COST = 12500;
constexpr int64_t EIP_7702_REFUND_PER_EXISTING_AUTHORITY = 12500;

constexpr size_t EIP_7702_DELEGATION_CODE_SIZE = bcos::executor::EIP7702_DELEGATION_CODE_SIZE;
constexpr size_t EIP_7702_DELEGATION_TARGET_OFFSET =
    bcos::executor::EIP7702_DELEGATION_TARGET_OFFSET;
constexpr size_t EIP_7702_MAX_AUTHORIZATION_LIST_SIZE =
    bcos::executor::EIP7702_MAX_AUTHORIZATION_LIST_SIZE;

// Reference alias: `auto` copy would decay the array and break std::begin/std::end.
inline constexpr auto const& EIP_7702_DELEGATION_PREFIX = bcos::executor::EIP7702_DELEGATION_PREFIX;

}  // namespace bcos::executor_v1
