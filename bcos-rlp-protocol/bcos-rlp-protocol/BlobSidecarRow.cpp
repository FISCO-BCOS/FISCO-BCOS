// Copyright (C) 2026 FISCO BCOS. SPDX-License-Identifier: Apache-2.0
// @file BlobSidecarRow.cpp
#include "BlobSidecarRow.h"
#include "Web3BlobTxWrapper.h"
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-utilities/Exceptions.h>
#include <span>

using bcos::codec::rlp::DecodingError;
using bcos::codec::rlp::throwRlpDecodeError;

bcos::bytes bcos::rpc::encodeBlobSidecarRow(engine::BlobsBundleV1 const& bundle)
{
    auto const blobCount = bundle.blobs.size();
    if (blobCount == 0 || bundle.commitments.size() != blobCount ||
        bundle.proofs.size() % blobCount != 0) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(bcos::Exception{} << bcos::errinfo_comment(
                                  "encodeBlobSidecarRow: bundle counts do not line up"));
    }
    auto const proofsPerBlob = bundle.proofs.size() / blobCount;
    bcos::bytes payload;
    for (std::size_t i = 0; i < blobCount; ++i)
    {
        bcos::bytes item;
        codec::rlp::encode(item, bundle.commitments[i]);
        codec::rlp::encode(item, bundle.blobs[i]);
        codec::rlp::encode(item,
            std::span<const bcos::bytes>(bundle.proofs.data() + i * proofsPerBlob, proofsPerBlob));
        codec::rlp::encodeHeader(
            payload, codec::rlp::Header{.isList = true, .payloadLength = item.size()});
        payload.insert(payload.end(), item.begin(), item.end());
    }
    bcos::bytes row;
    row.reserve(payload.size() + 9);
    codec::rlp::encodeHeader(
        row, codec::rlp::Header{.isList = true, .payloadLength = payload.size()});
    row.insert(row.end(), payload.begin(), payload.end());
    return row;
}

std::vector<bcos::engine::BlobItem> bcos::rpc::decodeBlobSidecarRow(bcos::bytesConstRef row)
{
    bcos::bytes mutableRow(row.begin(), row.end());
    bcos::bytesRef in = bcos::ref(mutableRow);
    auto const outerHead = codec::rlp::decodeHeader(in);
    if (!outerHead.isList || outerHead.payloadLength != in.size()) [[unlikely]]
    {
        throwRlpDecodeError(DecodingError::UnexpectedString, "blob sidecar row: not one list");
    }
    std::vector<engine::BlobItem> items;
    while (!in.empty())
    {
        bcos::bytes commitment;
        bcos::bytes blob;
        std::vector<bcos::bytes> proofs;
        codec::rlp::decode(in, commitment, blob, proofs);
        if (commitment.size() != BLOB_TX_COMMITMENT_SIZE || blob.size() != BLOB_TX_BLOB_SIZE ||
            (proofs.size() != 1 && proofs.size() != engine::CELL_PROOFS_PER_BLOB) ||
            std::ranges::any_of(proofs,
                [](auto const& proof) { return proof.size() != BLOB_TX_PROOF_SIZE; })) [[unlikely]]
        {
            throwRlpDecodeError(DecodingError::UnexpectedListElements,
                "blob sidecar row: item has unexpected sizes");
        }
        items.push_back(engine::BlobItem{.version = proofs.size() == engine::CELL_PROOFS_PER_BLOB ?
                                                        engine::BLOB_SIDECAR_VERSION_1 :
                                                        engine::BLOB_SIDECAR_VERSION_0,
            .commitment = std::move(commitment),
            .proofs = std::move(proofs),
            .blob = std::move(blob)});
    }
    return items;
}
