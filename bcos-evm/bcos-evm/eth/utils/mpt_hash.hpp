// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2022 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <bcos-evm/eth/state/hash_utils.hpp>
#include <span>

namespace evmone::test
{
class TestState;
}

namespace evmone::state
{
/// The hash of the empty Merkle Patricia Trie.
///
/// Specifically, this is the value of keccak256(RLP("")), i.e. keccak256({0x80}).
constexpr auto EMPTY_MPT_HASH =
    0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421_bytes32;

/// Computes Merkle Patricia Trie root hash for the given collection of state accounts.
hash256 mpt_hash(const test::TestState& state);

/// Computes Merkle Patricia Trie root hash for the given list of structures.
template <typename T>
hash256 mpt_hash(std::span<const T> list);

/// A helper to automatically convert collections (e.g. vector, array) to span.
template <typename T>
inline hash256 mpt_hash(const T& list)
    requires std::is_convertible_v<T, std::span<const typename T::value_type>>
{
    return mpt_hash(std::span<const typename T::value_type>{list});
}

}  // namespace evmone::state
