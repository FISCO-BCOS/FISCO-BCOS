// Copyright (C) 2026 FISCO BCOS. SPDX-License-Identifier: Apache-2.0
// @file BlobSidecarRow.h
// @brief The committed-block blob sidecar row (SYS_NUMBER_2_BLOBS, one per block the EL lane
// builds): rlp([[commitment, blob, [proof, ...]], ...]) in block order, each item one blob
// with its slice of the block's BlobsBundle proofs — one EIP-4844 blob proof, or the
// CELL_PROOFS_PER_BLOB EIP-7594 cell proofs of an Osaka block, which is also how a reader
// tells the sidecar version. The one codec the commit writer, the engine_getBlobs reader and
// the tests share, so the layout exists in one place.
#pragma once

#include <bcos-framework/engine/Types.h>
#include <bcos-utilities/Common.h>
#include <vector>

namespace bcos::rpc
{
/// Encode a block's bundle as the sidecar row. The bundle's proofs are one flat list, the
/// same count per blob (the block's sidecar version); a bundle whose counts do not line up
/// throws bcos::Exception.
bcos::bytes encodeBlobSidecarRow(engine::BlobsBundleV1 const& bundle);

/// Decode the sidecar row into one BlobItem per blob (version from the proofs count). Throws
/// codec::rlp::RlpDecodeException on a malformed row — a commitment that is not 48 bytes, a
/// blob that is not BLOB_TX_BLOB_SIZE bytes, or a proofs list that is neither 1 nor
/// CELL_PROOFS_PER_BLOB entries of 48 bytes.
std::vector<engine::BlobItem> decodeBlobSidecarRow(bcos::bytesConstRef row);
}  // namespace bcos::rpc
