// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2022 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "authorization.hpp"
#include "blob_params.hpp"
#include "bloom_filter.hpp"
#include "state_diff.hpp"
#include <intx/intx.hpp>
#include <optional>
#include <vector>

namespace evmone::state
{
/// The maximum allowed gas limit for a transaction (EIP-7825).
constexpr auto MAX_TX_GAS_LIMIT = 0x1000000;  // 2**24

using AccessList = std::vector<std::pair<address, std::vector<bytes32>>>;

/// Decodes an EIP-7702 authorization.
///
/// Declared here (not file-local) so the generic rlp::decode(std::vector<T>&) finds it by ADL.
[[nodiscard]] bool decode(bytes_view& from, Authorization& to) noexcept;

struct Transaction
{
    /// The type of the transaction.
    ///
    /// The format is defined by EIP-2718: Typed Transaction Envelope.
    /// https://eips.ethereum.org/EIPS/eip-2718.
    enum class Type : uint8_t
    {
        /// The legacy RLP-encoded transaction without leading "type" byte.
        legacy = 0,

        /// The typed transaction with optional account/storage access list.
        /// Introduced by EIP-2930 https://eips.ethereum.org/EIPS/eip-2930.
        access_list = 1,

        /// The typed transaction with priority gas price.
        /// Introduced by EIP-1559 https://eips.ethereum.org/EIPS/eip-1559.
        eip1559 = 2,

        /// The typed blob transaction (with array of blob hashes).
        /// Introduced by EIP-4844 https://eips.ethereum.org/EIPS/eip-4844.
        blob = 3,

        /// The typed set code transaction (with authorization list).
        /// Introduced by EIP-7702 https://eips.ethereum.org/EIPS/eip-7702.
        set_code = 4,
    };

    /// Returns amount of blob gas used by this transaction
    [[nodiscard]] uint64_t blob_gas_used() const { return GAS_PER_BLOB * blob_hashes.size(); }

    /// Whether the transaction specifies expected chain id. Always true for typed transactions.
    [[nodiscard]] bool chain_id_protected() const noexcept
    {
        return type != Type::legacy || v >= 35;
    }

    Type type = Type::legacy;
    bytes data;
    int64_t gas_limit = 0;
    intx::uint256 max_gas_price;
    intx::uint256 max_priority_gas_price;
    intx::uint256 max_blob_gas_price;
    address sender;
    std::optional<address> to;
    intx::uint256 value;
    AccessList access_list;
    std::vector<bytes32> blob_hashes;
    uint64_t chain_id = 0;
    uint64_t nonce = 0;
    intx::uint256 r;
    intx::uint256 s;

    /// The verbatim v value of the signature.
    /// It encodes y_parity and for legacy transactions chain id.
    uint64_t v = 0;
    AuthorizationList authorization_list;
};

/// Decodes a transaction from its complete serialization @p data.
///
/// Handles the legacy RLP list and the EIP-2718 typed envelope (type byte followed by an RLP list).
[[nodiscard]] std::optional<Transaction> decode_transaction(bytes_view data) noexcept;

/// Recovers the sender (the signer) of the transaction @p tx decoded from @p txbytes,
/// or std::nullopt if the signature is invalid.
///
/// The serialization is needed as well because the signing preimage is a slice of it; @p tx must
/// be what decode_transaction(@p txbytes) returned.
///
/// The recovery is strict at every revision: r, s in [1, secp256k1n) and low s (EIP-2).
[[nodiscard]] std::optional<address> recover_sender(
    const Transaction& tx, bytes_view txbytes) noexcept;

/// Transaction properties computed during the validation needed for the execution.
struct TransactionProperties
{
    /// The amount of gas provided to the EVM for the transaction execution.
    int64_t execution_gas_limit = 0;

    /// The amount of state-gas spendable by EVM on state increase (since EIP-8037).
    int64_t state_gas_limit = 0;

    /// The minimal amount of gas the transaction must use.
    int64_t min_gas_cost = 0;
};

struct Log
{
    address addr;
    bytes data;
    std::vector<bytes32> topics;
};

/// Transaction Receipt
///
/// This struct is used in two contexts:
/// 1. As the formally specified, RLP-encode transaction receipt included in the Ethereum blocks.
/// 2. As the internal representation of the transaction execution result.
/// These both roles share most, but not all the information. There are some fields that cannot be
/// assigned in the single transaction execution context. There are also fields that are not a part
/// of the RLP-encoded transaction receipts.
/// TODO: Consider splitting the struct into two based on the duality explained above.
struct TransactionReceipt
{
    Transaction::Type type = Transaction::Type::legacy;
    evmc_status_code status = EVMC_INTERNAL_ERROR;

    /// Amount of gas used by this transaction (after refund, with the min gas applied).
    int64_t gas_used = 0;

    /// Amount of gas counted against the block gas limit by this transaction (EIP-7778).
    int64_t block_gas_used = 0;

    /// The amount of state-gas used by this transaction (since EIP-8037).
    /// It is the state-gas part of #block_gas_used; the rest is the execution-gas part.
    int64_t state_gas_used = 0;

    /// Amount of gas used by this and previous transactions in the block.
    int64_t cumulative_gas_used = 0;
    std::vector<Log> logs;
    BloomFilter logs_bloom_filter;
    StateDiff state_diff;

    /// Root hash of the state after this transaction. Used only in old pre-Byzantium transactions.
    std::optional<bytes32> post_state;
};

}  // namespace evmone::state
