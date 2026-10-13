// Copyright (C) 2026 FISCO BCOS. SPDX-License-Identifier: Apache-2.0
// @file Web3BlobTxWrapper.h
// @brief EIP-4844 network-wrapper codec for blob (type-3) transactions:
//   0x03 || rlp([tx_payload_body, blobs, commitments, proofs])
// The wrapper is the mempool/gossip form; the ExecutionPayload and the tx hash always use
// the stripped form (tx_payload_body alone, decoded by EIP4844TxHandler).
#pragma once

#include "Web3Transaction.h"
#include <bcos-framework/engine/Types.h>

namespace bcos::rpc
{
// Local copies of the c-kzg sizes: bcos-rlp-protocol builds in every configuration while
// the c-kzg headers are a fullnode-only dependency (bcos-crypto/kzg/Kzg4844.h).
inline constexpr std::size_t BLOB_TX_BLOB_SIZE = 131072;
inline constexpr std::size_t BLOB_TX_COMMITMENT_SIZE = 48;
inline constexpr std::size_t BLOB_TX_PROOF_SIZE = 48;

/// True when the 0x03 envelope is the network wrapper rather than the stripped form: after
/// the outer list header, a wrapper's first item (the tx body) is itself an RLP list, while
/// a stripped tx's first item (chain_id) is always a string. False on any malformed shape
/// (callers treat "not a wrapper" as "decode as stripped", which reports its own errors).
bool isBlobTxNetworkWrapper(bcos::bytesConstRef envelope) noexcept;

/// Decode the network wrapper into the stripped transaction and its sidecar. Throws
/// codec::rlp::RlpDecodeException on malformed input. Validates the EIP-4844 wrapper
/// invariants: the outer list holds exactly the four items, every blob is exactly
/// BLOB_TX_BLOB_SIZE bytes, every commitment/proof exactly 48 bytes, and the three
/// sidecar lists are the same length as the transaction's blobVersionedHashes.
void decodeBlobTxNetworkWrapper(
    bcos::bytesConstRef envelope, Web3Transaction& txOut, engine::BlobTxSidecar& sidecarOut);

/// Encode the network wrapper (devp2p broadcast / tests). The sidecar lists must match the
/// transaction's blobVersionedHashes in length; a mismatch throws bcos::Exception.
bcos::bytes encodeBlobTxNetworkWrapper(
    const Web3Transaction& tx, const engine::BlobTxSidecar& sidecar);
}  // namespace bcos::rpc
