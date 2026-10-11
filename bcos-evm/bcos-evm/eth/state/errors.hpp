// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2023 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cassert>
#include <system_error>

namespace evmone::state
{

/// The reasons a transaction or a block is rejected.
///
/// The message of each is the execution-spec-tests exception name for the same rule, so a test can
/// compare a rejection against the `expectException` its fixture states. Where the specs name more
/// than one exception for a rule, the message is the canonical one and the test harness carries the
/// alternatives; the few rules the specs do not name at all keep a plain message.
enum ErrorCode : int  // NOLINT(*-use-enum-class)
{
    SUCCESS = 0,
    INTRINSIC_GAS_TOO_LOW,
    TYPE_NOT_SUPPORTED,
    INSUFFICIENT_ACCOUNT_FUNDS,
    NONCE_IS_MAX,
    NONCE_TOO_HIGH,
    NONCE_TOO_LOW,
    PRIORITY_GREATER_THAN_MAX_FEE_PER_GAS,
    INSUFFICIENT_MAX_FEE_PER_GAS,
    INSUFFICIENT_MAX_FEE_PER_BLOB_GAS,
    GAS_ALLOWANCE_EXCEEDED,
    SENDER_NOT_EOA,
    INITCODE_SIZE_EXCEEDED,
    CREATE_BLOB_TX,
    EMPTY_BLOB_HASHES_LIST,
    INVALID_BLOB_HASH_VERSION,
    BLOB_GAS_LIMIT_EXCEEDED,
    CREATE_SET_CODE_TX,
    EMPTY_AUTHORIZATION_LIST,
    GAS_LIMIT_EXCEEDS_MAXIMUM,
    INVALID_CHAIN_ID,
    INVALID_ENCODING,
    INVALID_SIGNATURE,
    UNKNOWN_ERROR,

    // Block-level validation.
    INCORRECT_BLOCK_FORMAT,
    INVALID_GASLIMIT,
    INVALID_BASEFEE_PER_GAS,
    INCORRECT_EXCESS_BLOB_GAS,
    RLP_BLOCK_LIMIT_EXCEEDED,
    INVALID_BLOCK_TIMESTAMP_OLDER_THAN_PARENT,
    UNKNOWN_PARENT,
    INVALID_BLOCK_NUMBER,

    // Block requests collection (EIP-7685).
    INVALID_DEPOSIT_EVENT_LAYOUT,
    SYSTEM_CONTRACT_EMPTY,
    SYSTEM_CONTRACT_CALL_FAILED,
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
                return "TransactionException.INTRINSIC_GAS_TOO_LOW";
            case TYPE_NOT_SUPPORTED:
                return "TransactionException.TYPE_NOT_SUPPORTED";
            case INSUFFICIENT_ACCOUNT_FUNDS:
                return "TransactionException.INSUFFICIENT_ACCOUNT_FUNDS";
            case NONCE_IS_MAX:
                return "TransactionException.NONCE_IS_MAX";
            case NONCE_TOO_HIGH:
                return "TransactionException.NONCE_MISMATCH_TOO_HIGH";
            case NONCE_TOO_LOW:
                return "TransactionException.NONCE_MISMATCH_TOO_LOW";
            case PRIORITY_GREATER_THAN_MAX_FEE_PER_GAS:
                return "TransactionException.PRIORITY_GREATER_THAN_MAX_FEE_PER_GAS";
            case INSUFFICIENT_MAX_FEE_PER_GAS:
                return "TransactionException.INSUFFICIENT_MAX_FEE_PER_GAS";
            case INSUFFICIENT_MAX_FEE_PER_BLOB_GAS:
                return "TransactionException.INSUFFICIENT_MAX_FEE_PER_BLOB_GAS";
            case GAS_ALLOWANCE_EXCEEDED:
                return "TransactionException.GAS_ALLOWANCE_EXCEEDED";
            case SENDER_NOT_EOA:
                return "TransactionException.SENDER_NOT_EOA";
            case INITCODE_SIZE_EXCEEDED:
                return "TransactionException.INITCODE_SIZE_EXCEEDED";
            case CREATE_BLOB_TX:
                return "TransactionException.TYPE_3_TX_CONTRACT_CREATION";
            case EMPTY_BLOB_HASHES_LIST:
                return "TransactionException.TYPE_3_TX_ZERO_BLOBS";
            case INVALID_BLOB_HASH_VERSION:
                return "TransactionException.TYPE_3_TX_INVALID_BLOB_VERSIONED_HASH";
            case BLOB_GAS_LIMIT_EXCEEDED:
                return "TransactionException.TYPE_3_TX_BLOB_COUNT_EXCEEDED";
            case CREATE_SET_CODE_TX:
                return "TransactionException.TYPE_4_TX_CONTRACT_CREATION";
            case EMPTY_AUTHORIZATION_LIST:
                return "TransactionException.TYPE_4_EMPTY_AUTHORIZATION_LIST";
            case GAS_LIMIT_EXCEEDS_MAXIMUM:
                return "TransactionException.GAS_LIMIT_EXCEEDS_MAXIMUM";
            case INVALID_CHAIN_ID:
                return "TransactionException.INVALID_CHAINID";
            case INVALID_ENCODING:
                // The execution specs name every way an encoding can be malformed separately
                // (RLP_*), so there is no single constant standing for this one.
                return "invalid transaction encoding";
            case INVALID_SIGNATURE:
                return "TransactionException.INVALID_SIGNATURE_VRS";
            case UNKNOWN_ERROR:
                return "unknown error";
            case INCORRECT_BLOCK_FORMAT:
                return "BlockException.INCORRECT_BLOCK_FORMAT";
            case INVALID_GASLIMIT:
                return "BlockException.INVALID_GASLIMIT";
            case INVALID_BASEFEE_PER_GAS:
                return "BlockException.INVALID_BASEFEE_PER_GAS";
            case INCORRECT_EXCESS_BLOB_GAS:
                return "BlockException.INCORRECT_EXCESS_BLOB_GAS";
            case RLP_BLOCK_LIMIT_EXCEEDED:
                return "BlockException.RLP_BLOCK_LIMIT_EXCEEDED";
            case INVALID_BLOCK_TIMESTAMP_OLDER_THAN_PARENT:
                return "BlockException.INVALID_BLOCK_TIMESTAMP_OLDER_THAN_PARENT";
            case UNKNOWN_PARENT:
                return "BlockException.UNKNOWN_PARENT";
            case INVALID_BLOCK_NUMBER:
                return "BlockException.INVALID_BLOCK_NUMBER";
            case INVALID_DEPOSIT_EVENT_LAYOUT:
                return "BlockException.INVALID_DEPOSIT_EVENT_LAYOUT";
            case SYSTEM_CONTRACT_EMPTY:
                return "BlockException.SYSTEM_CONTRACT_EMPTY";
            case SYSTEM_CONTRACT_CALL_FAILED:
                return "BlockException.SYSTEM_CONTRACT_CALL_FAILED";
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
