// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2024 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "hash_utils.hpp"
#include "transaction.hpp"
#include <evmc/evmc.hpp>
#include <span>

namespace evmone::state
{
/// The address of the deposit contract.
///
/// TODO: This address differs in different chains, so it should be configurable.
constexpr auto DEPOSIT_CONTRACT_ADDRESS = 0x00000000219ab540356cBB839Cbe05303d7705Fa_address;

/// The topic of deposit log of the deposit contract.
constexpr auto DEPOSIT_EVENT_SIGNATURE_HASH =
    0x649bbc62d0e31342afea4e5cd82d4049e7e1ee912fc0889aa790803be39038c5_bytes32;

/// `requests` object.
///
/// Defined by EIP-7685: General purpose execution layer requests.
/// https://eips.ethereum.org/EIPS/eip-7685.
struct Requests
{
    /// The type of the requests.
    enum class Type : uint8_t
    {
        /// Deposit requests.
        /// Introduced by EIP-6110 https://eips.ethereum.org/EIPS/eip-6110.
        deposit = 0,

        /// Withdrawal requests.
        /// Introduced by EIP-7002 https://eips.ethereum.org/EIPS/eip-7002.
        withdrawal = 1,

        /// Consolidation requests.
        /// Introduced by EIP-7251 https://eips.ethereum.org/EIPS/eip-7251.
        consolidation = 2,
    };

    /// Raw encoded data of requests object: first byte is type, the rest is request objects.
    bytes raw_data;

    explicit Requests(Type _type, bytes_view data = {})
    {
        raw_data.reserve(1 + data.size());
        raw_data += static_cast<uint8_t>(_type);
        raw_data += data;
    }

    /// Requests type.
    Type type() const noexcept { return static_cast<Type>(raw_data[0]); }

    /// Requests data - an opaque byte array, contains zero or more encoded request objects.
    bytes_view data() const noexcept { return {raw_data.data() + 1, raw_data.size() - 1}; }

    /// Append data to requests object byte array.
    void append(bytes_view data) { raw_data.append(data); }
};

/// Calculate commitment value of block requests list
hash256 calculate_requests_hash(std::span<const Requests> requests_list);

/// Construct a requests object from logs of the deposit contract.
///
/// @return The collected deposit requests or std::nullopt if the collection has failed.
std::optional<Requests> collect_deposit_requests(std::span<const TransactionReceipt> receipts);
}  // namespace evmone::state
