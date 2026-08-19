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
    int64_t timestamp{0};

    // Optional fields (16–23)
    std::optional<bcos::u256> baseFee;
    std::optional<bcos::h256> withdrawalsHash;
    std::optional<bcos::u256> blobGasUsed;
    std::optional<bcos::u256> excessBlobGas;
    std::optional<bcos::h256> parentBeaconRoot;
    std::optional<bcos::h256> requestsHash;
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
    /// NON_ETH (not copied): headers this produces are always treated as NON_ETH, so the RLP
    /// timestamp is SECONDS and the FISCO header stores MILLISECONDS, ×1000 is applied, and
    /// rlpEncode's /1000 pairs with it.
    static bcos::Error::UniquePtr decodeTarsHeader(
        bcos::protocol::BlockHeader::Ptr header, bcos::bytesConstRef _data);
    static bcos::Error::UniquePtr toEthBlockHeader(
        EthBlockHeader& ethHeader, bcos::bytesConstRef _data);
    static bcos::Error::UniquePtr calculateRLPHash(bcos::protocol::BlockHeader& header);
    /// Compute keccak256(rlp(header)) WITHOUT validation or state mutation — usable for
    /// FISCO-native/OP headers (EthBlockVersion::NON_ETH, timestamp ms → rlpEncode /1000) that
    /// calculateRLPHash's validateHeader rejects. Returns the 32-byte Ethereum block hash.
    /// Throws std::invalid_argument if a NON_ETH header's timestamp is not a whole number
    /// of seconds (ms not divisible by 1000) — callers that cannot tolerate exceptions
    /// should use calculateRLPHash (which returns Error::UniquePtr) instead.
    static bcos::crypto::HashType computeHash(const bcos::protocol::BlockHeader& header) noexcept(
        false);

    const EthBlockHeaderData& data() const { return m_data; }

private:
    EthBlockHeaderData m_data;
    EthBlockVersion m_version{EthBlockVersion::NON_ETH};
};

}  // namespace bcos::protocol
