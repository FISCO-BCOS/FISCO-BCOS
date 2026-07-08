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
#include <bcos-utilities/FixedBytes.h>

namespace bcos::ledger
{
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
}  // namespace bcos::ledger
