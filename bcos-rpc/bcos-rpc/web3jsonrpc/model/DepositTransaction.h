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
 * @file DepositTransaction.h
 * @brief OP Stack 0x7E deposit transaction: raw-bytes decoding and geth-shaped JSON
 */

#pragma once

#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/FixedBytes.h>
#include <json/json.h>
#include <optional>

namespace bcos::rpc
{

/// EIP-2718 type byte of an OP Stack deposit transaction.
inline constexpr uint8_t c_depositTxType = 0x7e;

/// Decoded form of an OP Stack deposit transaction (0x7E). Field set and semantics follow
/// op-geth core/types.DepositTx and the executor-side DepositTx in
/// bcos-evm/bcos-evm/opstack/OpTransition.h. This is the wire-decoding twin used by the
/// RPC layer; execution wiring converts it to the evmone-facing representation.
struct DepositTransaction
{
    crypto::HashType sourceHash;
    Address from;
    std::optional<Address> to;  ///< nullopt = contract creation
    std::optional<u256> mint;   ///< nullopt = no mint (RLP empty item, op-geth *big.Int nil)
    u256 value{0};
    uint64_t gas{0};
    bool isSystemTx{false};
    bytes input;
};

/// Decode `0x7e || rlp([sourceHash, from, to, mint, value, gas, isSystemTx, data])` from
/// the full raw envelope (type byte included). Consumes the decoded bytes from `in`;
/// trailing bytes inside the RLP list are an error. Returns nullptr on success.
///
/// Display-grade, not consensus-grade: bytes after the RLP list are not rejected,
/// non-canonical integer encodings are accepted, and over-long uint64 payloads truncate.
/// Execution wiring must NOT reuse this decoder for consensus-side validation.
bcos::Error::UniquePtr decodeDepositTransaction(
    bcos::bytesRef& in, DepositTransaction& out) noexcept;

/// Fill `result` with the deposit-specific JSON fields in op-geth's RPC shape
/// (internal/ethapi RPCTransaction): type 0x7e, sourceHash, from, to (null for creation),
/// gas, value, input, mint (only when present), isSystemTx (only when true). Fields
/// without deposit semantics follow op-geth: nonce/gasPrice/v/r/s are 0x0 (the deposit
/// nonce lives in the receipt and is filled by the receipt path once available), and no
/// chainId is emitted.
void combineDepositTxResponse(Json::Value& result, const DepositTransaction& deposit);

}  // namespace bcos::rpc
