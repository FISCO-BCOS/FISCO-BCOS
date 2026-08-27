/**
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file GenesisStateRoot.h
 * @brief op-geth-compatible Ethereum MPT state root over the genesis allocs.
 */
#pragma once
#include <bcos-framework/ledger/GenesisConfig.h>
#include <bcos-task/Task.h>
#include <bcos-tool/Exceptions.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Exceptions.h>
#include <bcos-utilities/FixedBytes.h>
#include <boost/algorithm/hex.hpp>
#include <boost/throw_exception.hpp>
#include <string>
#include <string_view>
#include <unordered_map>

namespace bcos::ledger
{
/// Strip a leading lowercase 0x prefix, if present. Genesis alloc hex arrives
/// 0x-prefixed from NodeConfig and unprefixed from direct GenesisConfig callers.
inline std::string_view stripHexPrefix(std::string_view hex)
{
    return hex.starts_with("0x") ? hex.substr(2) : hex;
}

/// unhex into a fixed-size buffer requires an EXACT digit count:
/// boost::algorithm::unhex writes past the buffer on over-long input and
/// silently zero-pads on short input, and bcos::fromHex pads odd/short input —
/// any of these corrupts the genesis state (or worse) instead of failing
/// loudly. Shared by the genesis trie hasher and both genesis-state importers
/// so the state root can never be computed over hex the importer would reject
/// (and vice versa).
inline void unhexAllocExact(
    std::string_view hex, std::string_view field, uint8_t* out, size_t expectedBytes)
{
    hex = stripHexPrefix(hex);
    if (hex.size() != expectedBytes * 2)
    {
        BOOST_THROW_EXCEPTION(
            bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                "genesis alloc " + std::string(field) + " must be exactly " +
                std::to_string(expectedBytes * 2) + " hex digits, got " +
                std::to_string(hex.size())));
    }
    boost::algorithm::unhex(hex.begin(), hex.end(), out);
}

/// Variable-length counterpart (contract code): even-length valid hex, decoded
/// to bytes. An empty body decodes to zero bytes ("no code").
inline bcos::bytes unhexAllocBytes(std::string_view hex, std::string_view field)
{
    hex = stripHexPrefix(hex);
    if (hex.size() % 2 != 0)
    {
        BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                  "genesis alloc " + std::string(field) +
                                  " must be even-length hex, got " + std::to_string(hex.size()) +
                                  " digits"));
    }
    bcos::bytes out;
    out.reserve(hex.size() / 2);
    try
    {
        boost::algorithm::unhex(hex.begin(), hex.end(), std::back_inserter(out));
    }
    catch (boost::exception const&)
    {
        BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << bcos::errinfo_comment(
                                  "genesis alloc " + std::string(field) + " is not valid hex"));
    }
    return out;
}
// Computes the Ethereum genesis state root over genesis.m_allocs, byte-identical
// to what go-ethereum / op-geth `Genesis.ToBlock()` produces for the same alloc
// set. This is the value op-node expects at `rollup.json` genesis.l2.hash's
// stateRoot, and the value the FISCO-BCOS Engine-API eth-block view serves as
// the genesis block's stateRoot.
//
// Construction (matches go-ethereum exactly):
//   - State trie is a SECURE trie: leaf key = keccak256(address20).
//   - Leaf value = RLP([nonce, balance, storageRoot, codeHash]) (Ethereum
//     StateAccount): nonce as a minimal big-endian RLP integer, balance the
//     same over u256, storageRoot/codeHash as 32-byte RLP strings.
//   - storageRoot is the SECURE storage trie over (keccak256(slot32) ->
//     RLP(value-with-leading-zero-bytes-trimmed)); empty storage ->
//     emptyRootHash(). A zero-valued slot is a no-op in Ethereum state and is
//     skipped (it must not change the storage root).
//   - codeHash = keccak256(code); empty code -> emptyCodeHash().
//   - Empty alloc set -> emptyRootHash() (the canonical empty-trie root
//     56e81f17...).
//
// MPT M3's HashBuilder produces canonical Ethereum trie roots (verified against
// go-ethereum vectors), so given correct per-account encoding the returned root
// is op-geth-compatible by construction.
bcos::task::Task<bcos::h256> computeGenesisStateRoot(GenesisConfig const& genesis);

/// The genesis state trie in full: the op-geth-compatible root plus EVERY node the build
/// produced — account trie and each account's storage sub-trie — as hash-keyed raw RLP.
/// Identical encodings across sub-tries hash identically and dedupe in the map.
struct GenesisStateTrie
{
    bcos::h256 root;
    std::unordered_map<bcos::h256, bcos::bytes> nodes;
};

// computeGenesisStateRoot plus the produced nodes. Scenario B (L2, Ethereum-compatible)
// chains need the nodes persisted as "/mpt/" state rows at genesis: block 1's incremental
// MPT build (buildAndCollect with the genesis root as parent) reads parent-version nodes
// through storage, and a missing node aborts execution loudly (MPTInvariantViolation).
bcos::task::Task<GenesisStateTrie> computeGenesisStateTrie(GenesisConfig const& genesis);
}  // namespace bcos::ledger
