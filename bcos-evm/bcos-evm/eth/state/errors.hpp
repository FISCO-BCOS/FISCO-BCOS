// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2023 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cassert>
#include <system_error>

namespace evmone::state
{

enum ErrorCode : int  // NOLINT(*-use-enum-class)
{
    SUCCESS = 0,
    INTRINSIC_GAS_TOO_LOW,
    TX_TYPE_NOT_SUPPORTED,
    INSUFFICIENT_FUNDS,
    NONCE_HAS_MAX_VALUE,
    NONCE_TOO_HIGH,
    NONCE_TOO_LOW,
    TIP_GT_FEE_CAP,
    FEE_CAP_LESS_THAN_BLOCKS,
    BLOB_FEE_CAP_LESS_THAN_BLOCKS,
    GAS_LIMIT_REACHED,
    SENDER_NOT_EOA,
    INIT_CODE_SIZE_LIMIT_EXCEEDED,
    CREATE_BLOB_TX,
    EMPTY_BLOB_HASHES_LIST,
    INVALID_BLOB_HASH_VERSION,
    BLOB_GAS_LIMIT_EXCEEDED,
    CREATE_SET_CODE_TX,
    EMPTY_AUTHORIZATION_LIST,
    MAX_GAS_LIMIT_EXCEEDED,
    UNKNOWN_ERROR,
};

/// Obtains a reference to the static error category object for evmone errors.
inline const std::error_category& evmone_category() noexcept
{
    struct Category : std::error_category
    {
        [[nodiscard]] const char* name() const noexcept final { return "evmone"; }

        [[nodiscard]] std::string message(int ev) const noexcept final
        {
            switch (ev)
            {
            case SUCCESS:
                return "";
            case INTRINSIC_GAS_TOO_LOW:
                return "intrinsic gas too low";
            case TX_TYPE_NOT_SUPPORTED:
                return "transaction type not supported";
            case INSUFFICIENT_FUNDS:
                return "insufficient funds for gas * price + value";
            case NONCE_HAS_MAX_VALUE:
                return "nonce has max value:";
            case NONCE_TOO_HIGH:
                return "nonce too high";
            case NONCE_TOO_LOW:
                return "nonce too low";
            case TIP_GT_FEE_CAP:
                return "max priority fee per gas higher than max fee per gas";
            case FEE_CAP_LESS_THAN_BLOCKS:
                return "max fee per gas less than block base fee";
            case BLOB_FEE_CAP_LESS_THAN_BLOCKS:
                return "max blob fee per gas less than block base fee";
            case GAS_LIMIT_REACHED:
                return "gas limit reached";
            case SENDER_NOT_EOA:
                return "sender not an eoa:";
            case INIT_CODE_SIZE_LIMIT_EXCEEDED:
                return "max initcode size exceeded";
            case CREATE_BLOB_TX:
                return "blob transaction must not be a create transaction";
            case EMPTY_BLOB_HASHES_LIST:
                return "empty blob hashes list";
            case INVALID_BLOB_HASH_VERSION:
                return "invalid blob hash version";
            case BLOB_GAS_LIMIT_EXCEEDED:
                return "blob gas limit exceeded";
            case CREATE_SET_CODE_TX:
                return "set code transaction must not be a create transaction";
            case EMPTY_AUTHORIZATION_LIST:
                return "empty authorization list";
            case MAX_GAS_LIMIT_EXCEEDED:
                return "max gas limit exceeded";
            case UNKNOWN_ERROR:
                return "Unknown error";
            default:
                assert(false);
                return "Wrong error code";
            }
        }
    };

    static const Category category_instance;
    return category_instance;
}

/// Creates error_code object out of an evmone error code value.
/// This is used by std::error_code to implement implicit conversion
/// evmone::ErrorCode -> std::error_code, therefore the definition is
/// in the global namespace to match the definition of ethash_errc.
inline std::error_code make_error_code(ErrorCode errc) noexcept
{
    return {errc, evmone_category()};
}

}  // namespace evmone::state
