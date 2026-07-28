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
 * @file OpDepositEncode.h
 * @brief OP 0x7E deposit transaction envelope encoder: rebuilds the raw transaction bytes
 *        (0x7E ‖ RLP(...)) from structured fields, per op-geth's
 *        core/types/deposit_tx.go DepositTx.
 * @date 2026-07-28
 */

#pragma once
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <cstdint>
#include <optional>

namespace bcos::codec::rlp
{

// Structured fields for one OP deposit transaction. Field names/semantics are aligned with the
// t8n vector corpus's `_op_deposit` JSON block (bcos-evm/test/opstack/t8n/cases/*.in.json) and
// op-geth's core/types/deposit_tx.go DepositTx (8-field RLP body, 0x7E envelope-prefixed):
// [sourceHash, from, to, mint, value, gas, isSystemTransaction, data].
//
// `mint`/`to` nilability mirrors op-geth's `*big.Int`/`*common.Address` `rlp:"nil"` semantics:
//   - `mint`: nil and a present-but-zero big.Int are RLP-indistinguishable (both encode to the
//     empty string 0x80), so `mint` is carried as a plain bcos::u256 defaulting to 0 — no
//     round-trip information is lost by dropping the optional wrapper here.
//   - `to`: MUST stay optional. nullopt (contract-creation deposit — 2 of the 39 deposit txs
//     across the 33-vector golden corpus are exactly this: isthmus_contract_create and
//     jovian_contract_create, tx index 1) encodes as the empty string, which is a *different*
//     byte sequence from a present-but-zero 20-byte address (which RLP-encodes as 0x94 ‖ 20
//     zero bytes). Collapsing the two would silently produce the wrong bytes for a real
//     contract-creation deposit.
struct OpDepositFields
{
    bcos::h256 sourceHash;
    bcos::Address from;
    std::optional<bcos::Address> to;  // nullopt = contract creation
    bcos::u256 mint{0};               // 0 == nil at the RLP level, see comment above
    bcos::u256 value{0};
    uint64_t gas{0};
    bool isSystemTransaction{false};
    bcos::bytes data;

    static constexpr uint8_t kDepositTxType{0x7e};
};

// 0x7E ‖ RLP([sourceHash, from, to, mint, value, gas, isSystemTransaction, data]) — field
// order/typing cross-checked byte-for-byte against op-geth's DepositTx.MarshalBinary() output
// via the Task 2 golden corpus (golden.rawTransactions[i] for every deposit-typed index; all
// 39 deposit transactions across the 33 golden vectors, spec §7.1/§7.5).
[[nodiscard]] bcos::bytes encodeDepositEnvelope(OpDepositFields const& fields);

}  // namespace bcos::codec::rlp
