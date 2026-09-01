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
 * @file Normalize.h
 * @brief Make a Web3 transaction's tars mirror equal to what its signature actually covers.
 * @date 2026/8/25
 */

#pragma once

#include "bcos-framework/protocol/Transaction.h"
#include "bcos-protocol/TransactionStatus.h"

namespace bcos::txvalidator
{

/// Rebuild a Web3 transaction's unauthenticated tars mirror from its signed envelope, and
/// reject it if the wire-supplied transaction hash disagrees with the recomputed one.
///
/// WHY THIS EXISTS
///
/// A Web3 transaction's signature covers only `extraTransactionBytes` (tars field 10) --
/// Transaction::verify() hashes exactly those bytes. Execution, however, reads the tars mirror:
/// EthereumTransition's build_message takes tx.to() / tx.input() / tx.value(), all of which
/// resolve to m_inner()->data.*. Nothing compares the two.
///
/// So a peer can take a victim's real transaction, keep field 3 (signature) and field 10
/// (envelope) byte-for-byte, and rewrite data.to and data.value. Signature recovery still yields
/// the victim; the canonical tx hash, being derived from the envelope, is still identical to the
/// honest transaction's. The funds move to the attacker. The identical hash has a second effect:
/// nodes that already cached the honest copy keep theirs (MemoryStorage dedups by hash) while
/// nodes that did not accept the forgery -- the same hash executing two different transfers, i.e.
/// a stateRoot split.
///
/// The same rewrite applies to web3TypedTxKind (mis-declaring a 1559 envelope as legacy to dodge
/// type-keyed checks) and to `attribute`, whose LIQUID_SCALE_CODEC / LIQUID_CREATE / DAG bits
/// change how BlockExecutive schedules and executes the transaction.
///
/// Issue #5364 is the narrow case of this for data.accessList, where the consequence is only gas
/// divergence.
///
/// WHAT IT DOES
///
/// Six steps, order fixed:
///   0. Outer tars `type` whitelist. BCOS transactions return None here and are not touched --
///      their dataHash already covers the whole TransactionData, so signature and execution read
///      the same bytes.
///   1. Classify the envelope through engine::dispatchRawTransaction. Blob / deposit ->
///      TxTypeNotSupported; unsupported or empty -> Malformed. This must run BEFORE the decode:
///      the decoder does not know 0x7e, so classifying afterwards would report a deposit as
///      Malformed instead.
///   2. Read the wire-supplied extraTransactionHash.
///   3. Decode the signed envelope.
///   4. Recompute the canonical tx hash from (envelope, signature) and compare. A mismatch is
///      InvalidSignature. An empty wire hash is accepted and filled in -- an older peer may
///      simply not have set it (the BCOS branch in TransactionFactoryImpl has the same
///      exemption).
///   5. Only now write anything: replace the whole TransactionData with the values decoded from
///      the envelope, zero the fields a Web3 transaction has no business carrying, and keep the
///      local bookkeeping (signature, sender, importTime, type, batch state).
///
/// Steps 0-4 do not modify @p tx at all, so a rejected transaction is left byte-identical. That
/// matters: a half-normalized transaction whose mirror was overwritten but whose hash check then
/// failed would otherwise be handed back to a caller holding only a status code.
///
/// WHERE TO CALL IT
///
/// On the untrusted ingress -- the peer-response path in bcos-txpool's TransactionSync -- and
/// before ANY read of tx->hash() there, because for a Web3 transaction TransactionImpl::hash()
/// returns the peer-supplied extraTransactionHash verbatim. There are two such reads, in this
/// order:
///
///   1. verifyFetchedTxs, `expectedHash != tx->hash()` -- the gate that checks the peer sent
///      the transactions we asked for.
///   2. importDownloadedTxs, `m_config->txpoolStorage()->exists(tx->hash())` -- the dedup
///      lookup, nine lines above the clearSenderAndHash() that drops the wire hash.
///
/// So the call site is in verifyFetchedTxs, ahead of (1) -- not inside importDownloadedTxs,
/// which would still let a forged hash through (1). Naming clearSenderAndHash() as the
/// constraint is likewise not enough: normalize() placed between (2) and it satisfies "before
/// clearSenderAndHash" while (2) still keys on a hash the peer chose. Once clearSenderAndHash()
/// has run there is nothing left for step 4 to compare against.
///
/// importDownloadedTxs is also reached from onGetMissedTxsFromLedger, where the transactions
/// come from the local ledger and none of this applies.
///
/// Not on the RPC ingress. There the tars mirror was built by takeToTarsTransaction() from
/// these exact envelope bytes inside the same call, so steps 3-5 provably reproduce what is
/// already in the struct -- a second RLP decode, a second keccak256 and a full re-projection
/// for a no-op. The forgery this defends against needs an attacker who can hand over a mirror
/// and an envelope that disagree, which the RPC path does not let anyone do.
///
/// @return None on success. Otherwise the transaction is unchanged.
protocol::TransactionStatus normalize(protocol::Transaction& tx);

}  // namespace bcos::txvalidator
