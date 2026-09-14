/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file Block.h
 * @brief A complete Ethereum block as assembled by the devp2p downloader.
 * @date 2026/8/18
 */
#pragma once

#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-rlp-protocol/EthBlockHeader.h>
#include <bcos-rlp-protocol/EthWithdrawal.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <optional>
#include <vector>

namespace bcos::devp2p::sync
{
// A block downloaded from a peer: the parsed header plus the opaque body
// elements (transactions/uncles/withdrawals keep their wire encoding so the
// block can be re-serialized losslessly).
struct Block
{
    bcos::protocol::EthBlockHeaderData header;
    bcos::h256 hash;       // keccak256(header RLP)
    bcos::bytes headerRlp;
    std::vector<bcos::bytes> transactions;   // opaque EIP-2718 encodings
    std::vector<bcos::bytes> uncles;         // raw header RLP (empty on PoS)
    std::optional<std::vector<bcos::bytes>> withdrawals;  // raw EIP-4895 RLP, Shanghai+

    uint64_t number() const { return static_cast<uint64_t>(header.number); }
    bcos::h256 parentHash() const { return header.parentInfo.blockHash; }
    bool hasWithdrawals() const { return withdrawals.has_value(); }
};

// The canonical empty-ommers hash and the header-hash function are single-sourced in
// bcos-rlp-protocol (bcos::protocol::c_emptyOmmersHash / bcos::protocol::ethHeaderHash,
// EthBlockHeader.h) — they used to be local copies here.
}  // namespace bcos::devp2p::sync
