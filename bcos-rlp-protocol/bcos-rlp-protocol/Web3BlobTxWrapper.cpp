// Copyright (C) 2026 FISCO BCOS. SPDX-License-Identifier: Apache-2.0
// @file Web3BlobTxWrapper.cpp
#include "Web3BlobTxWrapper.h"
#include "Web3TxHandler.h"
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <optional>

using bcos::codec::rlp::DecodingError;
using bcos::codec::rlp::decodeHeader;
using bcos::codec::rlp::throwRlpDecodeError;

namespace
{
// Minimal RLP header peek. Unlike codec::rlp::decodeHeader it does not require the declared
// payload to be present, so the wrapper probe can run on a short prefix of a potentially
// ~800KB envelope. Payload length itself is not needed here, only kind and header size.
struct PeekedHeader
{
    bool isList = false;
    std::size_t headerSize = 0;
};

std::optional<PeekedHeader> peekHeader(bcos::bytesConstRef in) noexcept
{
    if (in.empty())
    {
        return std::nullopt;
    }
    auto const headByte = static_cast<uint8_t>(in[0]);
    if (headByte < 0xb8)  // single byte or short string
    {
        return PeekedHeader{.isList = false, .headerSize = 1};
    }
    if (headByte < 0xc0)  // long string
    {
        auto const lenOfLen = static_cast<std::size_t>(headByte - 0xb7);
        if (in.size() < 1 + lenOfLen)
        {
            return std::nullopt;
        }
        return PeekedHeader{.isList = false, .headerSize = 1 + lenOfLen};
    }
    if (headByte < 0xf8)  // short list
    {
        return PeekedHeader{.isList = true, .headerSize = 1};
    }
    auto const lenOfLen = static_cast<std::size_t>(headByte - 0xf7);  // long list
    if (in.size() < 1 + lenOfLen)
    {
        return std::nullopt;
    }
    return PeekedHeader{.isList = true, .headerSize = 1 + lenOfLen};
}

// Decode one fixed-size-bytes list item (a blob / commitment / proof).
bcos::bytes decodeFixedBytesItem(bcos::bytesRef& in, std::size_t expectedSize, const char* what)
{
    auto const head = decodeHeader(in);
    if (head.isList || head.payloadLength != expectedSize) [[unlikely]]
    {
        throwRlpDecodeError(DecodingError::UnexpectedListElements,
            std::string("blob tx wrapper: ") + what + " has unexpected size");
    }
    bcos::bytes out(in.data(), in.data() + head.payloadLength);
    in = in.getCroppedData(head.payloadLength);
    return out;
}

std::vector<bcos::bytes> decodeFixedBytesList(
    bcos::bytesRef& in, std::size_t itemSize, const char* what)
{
    auto const head = decodeHeader(in);
    if (!head.isList) [[unlikely]]
    {
        throwRlpDecodeError(DecodingError::UnexpectedString,
            std::string("blob tx wrapper: ") + what + " must be a list");
    }
    bcos::byte* const payloadStart = in.data();
    std::vector<bcos::bytes> out;
    while (static_cast<std::size_t>(in.data() - payloadStart) < head.payloadLength)
    {
        out.push_back(decodeFixedBytesItem(in, itemSize, what));
    }
    if (static_cast<std::size_t>(in.data() - payloadStart) != head.payloadLength) [[unlikely]]
    {
        throwRlpDecodeError(DecodingError::UnexpectedListElements,
            std::string("blob tx wrapper: ") + what + " items cross the list boundary");
    }
    return out;
}
}  // namespace

bool bcos::rpc::isBlobTxNetworkWrapper(bcos::bytesConstRef envelope) noexcept
{
    if (envelope.size() < 2 || envelope[0] != static_cast<bcos::byte>(TransactionType::EIP4844))
    {
        return false;
    }
    // Only the two header bytes ranges matter, so the probe reads the envelope in place —
    // no copy of the (potentially ~800KB) body.
    bcos::bytesConstRef const rest(envelope.data() + 1, envelope.size() - 1);
    auto const outer = peekHeader(rest);
    if (!outer || !outer->isList || rest.size() <= outer->headerSize)
    {
        return false;
    }
    auto const first = peekHeader(bcos::bytesConstRef(
        rest.data() + outer->headerSize, rest.size() - outer->headerSize));
    return first && first->isList;
}

void bcos::rpc::decodeBlobTxNetworkWrapper(
    bcos::bytesConstRef envelope, Web3Transaction& txOut, engine::BlobTxSidecar& sidecarOut)
{
    if (!isBlobTxNetworkWrapper(envelope)) [[unlikely]]
    {
        throwRlpDecodeError(
            DecodingError::UnsupportedTransactionType, "not an EIP-4844 network wrapper");
    }
    // The codec walks mutable views; one copy of the envelope up front, every sub-view
    // below points into it.
    bcos::bytes mutableEnvelope(envelope.begin(), envelope.end());
    bcos::bytesRef in = bcos::ref(mutableEnvelope);
    in = in.getCroppedData(1);
    auto const outerHead = decodeHeader(in);
    bcos::byte* const outerPayloadStart = in.data();

    // Item 1: tx_payload_body — the bare RLP list of the stripped transaction. Reuse the
    // EIP-4844 handler by handing it the type byte followed by the body's verbatim bytes.
    bcos::bytesRef probe = in;
    auto const bodyHead = decodeHeader(probe);
    auto const bodyEncodedLength =
        codec::rlp::lengthOfLength(bodyHead.payloadLength) + bodyHead.payloadLength;
    if (bodyEncodedLength > in.size()) [[unlikely]]
    {
        throwRlpDecodeError(DecodingError::InputTooShort, "blob tx wrapper: truncated tx body");
    }
    bcos::bytes stripped;
    stripped.reserve(1 + bodyEncodedLength);
    stripped.push_back(static_cast<bcos::byte>(TransactionType::EIP4844));
    stripped.insert(stripped.end(), in.data(), in.data() + bodyEncodedLength);
    {
        bcos::bytesRef strippedRef = bcos::ref(stripped);
        handlerFor(TransactionType::EIP4844).decode(strippedRef, txOut, true);
        if (!strippedRef.empty()) [[unlikely]]
        {
            throwRlpDecodeError(DecodingError::UnexpectedListElements,
                "blob tx wrapper: trailing bytes after tx body");
        }
    }
    in = in.getCroppedData(bodyEncodedLength);

    // Items 2-4: blobs, commitments, proofs.
    sidecarOut.blobs = decodeFixedBytesList(in, BLOB_TX_BLOB_SIZE, "blob");
    sidecarOut.commitments = decodeFixedBytesList(in, BLOB_TX_COMMITMENT_SIZE, "commitment");
    sidecarOut.proofs = decodeFixedBytesList(in, BLOB_TX_PROOF_SIZE, "proof");

    if (static_cast<std::size_t>(in.data() - outerPayloadStart) != outerHead.payloadLength)
        [[unlikely]]
    {
        throwRlpDecodeError(DecodingError::UnexpectedListElements,
            "blob tx wrapper: items cross the outer list boundary");
    }
    if (sidecarOut.blobs.size() != sidecarOut.commitments.size() ||
        sidecarOut.blobs.size() != sidecarOut.proofs.size() ||
        sidecarOut.blobs.size() != txOut.blobVersionedHashes.size()) [[unlikely]]
    {
        throwRlpDecodeError(DecodingError::UnexpectedListElements,
            "blob tx wrapper: blobs/commitments/proofs count mismatch with versioned hashes");
    }
}

bcos::bytes bcos::rpc::encodeBlobTxNetworkWrapper(
    const Web3Transaction& tx, const engine::BlobTxSidecar& sidecar)
{
    if (sidecar.blobs.size() != sidecar.commitments.size() ||
        sidecar.blobs.size() != sidecar.proofs.size() ||
        sidecar.blobs.size() != tx.blobVersionedHashes.size()) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(bcos::Exception{} << bcos::errinfo_comment(
                                  "encodeBlobTxNetworkWrapper: sidecar count mismatch with "
                                  "versioned hashes"));
    }
    // The stripped encode is 0x03 || rlp(body); the wrapper embeds rlp(body) alone.
    auto stripped = handlerFor(TransactionType::EIP4844).encode(tx);
    if (stripped.size() < 2) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(bcos::Exception{} << bcos::errinfo_comment(
                                  "encodeBlobTxNetworkWrapper: transaction is not a blob tx"));
    }
    bcos::bytes payload(stripped.begin() + 1, stripped.end());
    codec::rlp::encode(payload, sidecar.blobs);
    codec::rlp::encode(payload, sidecar.commitments);
    codec::rlp::encode(payload, sidecar.proofs);
    bcos::bytes out;
    out.reserve(1 + 9 + payload.size());
    out.push_back(static_cast<bcos::byte>(TransactionType::EIP4844));
    codec::rlp::encodeHeader(out, codec::rlp::Header{.isList = true, .payloadLength = payload.size()});
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}
