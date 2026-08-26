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
 * @file EthBlockHeader.h
 * @brief Pure RLP bridge — converts between bcos::protocol::BlockHeader and Ethereum RLP
 * @date 2026/6/24
 */
#pragma once

#include "bcos-framework/protocol/BlockHeader.h"
#include "bcos-framework/protocol/Protocol.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/interfaces/crypto/CommonType.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-utilities/Bloom.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/FixedBytes.h>
#include <memory>
#include <optional>

namespace bcos::protocol
{

struct EthBlockHeaderData
{
    // Non-optional fields (1–15), present in every Eth header version.
    bcos::Bloom logsBloom{};
    ParentInfo parentInfo;
    bcos::crypto::HashType uncleHash;
    bcos::crypto::HashType stateRoot;
    bcos::crypto::HashType txsRoot;
    bcos::crypto::HashType receiptsRoot;
    bcos::u256 difficulty{0};
    bcos::u256 gasLimit{0};
    bcos::u256 gasUsed{0};
    bcos::h256 prevRandao;
    bcos::bytes extraData;
    bcos::Address coinbase;
    bcos::h64 nonce;
    int64_t number{0};
    // WIRE SECONDS, unconditionally — the same unit in every owner (the EthBlockHeaderData
    // codec, EthBlock, ommers, and EthBlockHeader::data()). The internal BlockHeader's
    // millisecond timestamp lives in EthBlockHeader::m_timestampMs and is converted to/from
    // this field only at the rlpEncode/rlpDecode bridge (rlpEncode /1000, rlpDecode ×1000).
    int64_t timestamp{0};

    // Optional fields (16–23)
    std::optional<bcos::u256> baseFee;
    std::optional<bcos::h256> withdrawalsHash;
    std::optional<bcos::u256> blobGasUsed;
    std::optional<bcos::u256> excessBlobGas;
    std::optional<bcos::h256> parentBeaconRoot;
    std::optional<bcos::h256> requestsHash;

    bool operator==(const EthBlockHeaderData& rhs) const
    {
        return logsBloom == rhs.logsBloom && parentInfo == rhs.parentInfo &&
               uncleHash == rhs.uncleHash && stateRoot == rhs.stateRoot && txsRoot == rhs.txsRoot &&
               receiptsRoot == rhs.receiptsRoot && difficulty == rhs.difficulty &&
               gasLimit == rhs.gasLimit && gasUsed == rhs.gasUsed && prevRandao == rhs.prevRandao &&
               extraData == rhs.extraData && coinbase == rhs.coinbase && nonce == rhs.nonce &&
               number == rhs.number && timestamp == rhs.timestamp && baseFee == rhs.baseFee &&
               withdrawalsHash == rhs.withdrawalsHash && blobGasUsed == rhs.blobGasUsed &&
               excessBlobGas == rhs.excessBlobGas && parentBeaconRoot == rhs.parentBeaconRoot &&
               requestsHash == rhs.requestsHash;
    }
    bool operator!=(const EthBlockHeaderData& rhs) const { return !(*this == rhs); }
};

// Error codes for EthBlockHeader conversion failures.
enum class EthBlockHeaderError : int32_t
{
    InvalidHeader = 1,      // header misses a required Eth field
    RlpDecodeFailed = 2,    // RLP decoding failed
    InvalidHeaderType = 3,  // header type mismatch
};

class EthBlockHeader
{
public:
    EthBlockHeader() = default;

    explicit EthBlockHeader(const bcos::protocol::BlockHeader& header);

    void rlpEncode(bcos::bytes& out) const;
    bcos::Error::UniquePtr rlpDecode(bcos::bytesConstRef data);

    // The fork version this header's fields correspond to (derived from the presence of
    // optional fields after rlpDecode). 0/NON_ETH means no Eth fields were decoded.
    EthBlockVersion version() const { return m_version; }
    void setVersion(EthBlockVersion _version) { m_version = _version; }

    // Validate that the header carries every field its EthBlockVersion requires. On the
    // first missing field, fills `error` with an InvalidHeader error and returns false.
    // Returns true if the header is complete for its version.
    static bool validateHeader(
        const bcos::protocol::BlockHeader& _header, bcos::Error::UniquePtr& error);

    // Static helpers for the common upper-layer flows. Each returns a bcos::Error::UniquePtr
    // (null on success) and takes the destination object as an in/out parameter:
    //  - toTarsHeader: decode an RLP header into the caller-provided base-class header
    //    (writes all fields via the setter interface, sets its EthBlockVersion).
    //  - toEthBlockHeader: decode an RLP header into the caller-provided EthBlockHeader.
    //  - calculateRLPHash: compute keccak256(rlp(header)) and inject it into the
    //    base-class header via setRLPHash.
    static bcos::Error::UniquePtr toTarsHeader(
        bcos::protocol::BlockHeader::Ptr header, bcos::bytesConstRef _data);
    /// Like toTarsHeader but WITHOUT validateHeader — usable for FISCO-native/OP (NON_ETH)
    /// headers that validateHeader rejects. Unlike toTarsHeader, ethBlockVersion is PINNED to
    /// NON_ETH (not copied). The produced header's timestamp is MILLISECONDS like every other
    /// internal header (the RLP surface's seconds are converted by rlpDecode, unconditionally
    /// for every version).
    static bcos::Error::UniquePtr decodeTarsHeader(
        bcos::protocol::BlockHeader::Ptr header, bcos::bytesConstRef _data);
    static bcos::Error::UniquePtr toEthBlockHeader(
        EthBlockHeader& ethHeader, bcos::bytesConstRef _data);
    static bcos::Error::UniquePtr calculateRLPHash(bcos::protocol::BlockHeader& header);
    /// Compute keccak256(rlp(header)) WITHOUT validation or state mutation — usable for
    /// FISCO-native/OP headers (EthBlockVersion::NON_ETH) that calculateRLPHash's
    /// validateHeader rejects. Returns the 32-byte Ethereum block hash.
    /// The header's timestamp is internal milliseconds (every version); the RLP surface
    /// carries seconds, converted at the rlpEncode/rlpDecode bridge — rlpEncode throws
    /// std::invalid_argument if the internal timestamp is not a whole number of seconds.
    /// Callers that cannot tolerate exceptions should use calculateRLPHash (which returns
    /// Error::UniquePtr) instead.
    static bcos::crypto::HashType computeHash(const bcos::protocol::BlockHeader& header) noexcept(
        false);

    const EthBlockHeaderData& data() const { return m_data; }

    // Internal-domain (milliseconds) timestamp, mirroring the base BlockHeader. The
    // EthBlockHeaderData::timestamp member above is always wire seconds.
    int64_t timestampMs() const { return m_timestampMs; }

private:
    EthBlockHeaderData m_data;
    int64_t m_timestampMs{0};
    EthBlockVersion m_version{EthBlockVersion::NON_ETH};
};

}  // namespace bcos::protocol

namespace bcos::codec::rlp
{
// Codec overloads for the pure-data header struct, so EthBlockHeaderData can be embedded in
// larger Ethereum structures (block bodies, uncle lists, ...) with the same canonical field
// order as EthBlockHeader::rlpEncode/rlpDecode. EthBlockHeader::rlpEncode/rlpDecode delegate
// here, so the field order lives in exactly one place.
size_t length(const protocol::EthBlockHeaderData& _headerData) noexcept;
void encode(bcos::bytes& _out, const protocol::EthBlockHeaderData& _headerData) noexcept;
bcos::Error::UniquePtr decode(
    bcos::bytesRef& _in, protocol::EthBlockHeaderData& _headerData) noexcept;
}  // namespace bcos::codec::rlp

namespace bcos::protocol
{
// ADL-visible delegators (see EthLog.h): let EthBlockHeaderData participate in
// std::vector<EthBlockHeaderData> (ommers) / variadic-list encode/decode.
inline size_t length(const EthBlockHeaderData& _headerData) noexcept
{
    return codec::rlp::length(_headerData);
}
inline void encode(bcos::bytes& _out, const EthBlockHeaderData& _headerData) noexcept
{
    codec::rlp::encode(_out, _headerData);
}
inline bcos::Error::UniquePtr decode(bcos::bytesRef& _in, EthBlockHeaderData& _headerData) noexcept
{
    return codec::rlp::decode(_in, _headerData);
}
}  // namespace bcos::protocol
