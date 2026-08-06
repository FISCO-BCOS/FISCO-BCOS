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
 * @brief Pure RLP bridge — converts between bcostars::BlockHeader and Ethereum RLP
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

namespace bcostars
{
struct BlockHeader;
}

namespace bcos::protocol
{

struct EthBlockHeaderData
{
    // Non-optional fields (1–15)
    bcos::Bloom logsBloom{};
    ParentInfo parentInfo;
    bcos::crypto::HashType stateRoot;
    bcos::crypto::HashType txsRoot;
    bcos::crypto::HashType receiptsRoot;
    bcos::u256 gasLimit{0};
    bcos::u256 gasUsed{0};
    bcos::h256 prevRandao;
    bcos::bytes extraData;
    bcos::Address coinbase;
    int64_t number{0};
    int64_t timestamp{0};
    // the following fields are discarded
    // uncleHash, difficulty, nonce

    // Optional fields (16–23)
    std::optional<bcos::u256> baseFee;
    std::optional<bcos::h256> withdrawalsHash;
    std::optional<bcos::u256> blobGasUsed;
    std::optional<bcos::u256> excessBlobGas;
    std::optional<bcos::h256> parentBeaconRoot;
    std::optional<bcos::h256> requestsHash;
    // blockAccessListHash / slotNumber are FISCO-internal fields: they do NOT belong to the
    // Ethereum-standard header schema, so rlpEncode/rlpDecode/toTarsHeader never touch them.
    // They are read from Tars into EthBlockHeaderData purely for introspection; a
    // Tars→RLP→Tars round-trip will drop them by design.
    std::optional<bcos::h256> blockAccessListHash;
    std::optional<uint64_t> slotNumber;
};

// Error codes for EthBlockHeader conversion failures.
enum class EthBlockHeaderError : int32_t
{
    InvalidTarsHeader = 1,  // Tars header misses a required Eth field
    RlpDecodeFailed = 2,    // RLP decoding failed
    InvalidHeaderType = 3,  // header is not a bcostars::protocol::BlockHeaderImpl
};

class EthBlockHeader
{
public:
    EthBlockHeader() = default;

    explicit EthBlockHeader(const bcostars::BlockHeader& tarsHeader);

    void rlpEncode(bcos::bytes& out) const;
    bcos::Error::UniquePtr rlpDecode(bcos::bytesConstRef data);

    // Validate that the Tars header carries every Ethereum-required field. On the first
    // missing/short field, fills `error` with an InvalidTarsHeader error and returns false.
    // Returns true if the header is complete.
    static bool validateTarsHeader(
        const bcostars::BlockHeader& _tarsHeader, bcos::Error::UniquePtr& error);

    // Static helpers for the common upper-layer flows. Each takes a bcos::Error::UniquePtr&
    // output parameter: on success it is left null and a valid value is returned; on failure
    // it is filled with an error describing the problem and an empty result is returned.
    //  - toTarsHeader: decode an RLP header into a base-class BlockHeader::Ptr
    //    (concrete type bcostars::protocol::BlockHeaderImpl, marked as an Eth header).
    //  - toEthBlockHeader: decode an RLP header into an EthBlockHeader value.
    //  - calculateRLPHash: compute keccak256(rlp(header)) and inject it into the
    //    Tars-backed BlockHeaderImpl via setRLPHash.
    static bcos::protocol::BlockHeader::Ptr toTarsHeader(
        bcos::bytesConstRef _data, bcos::Error::UniquePtr& error);
    static EthBlockHeader toEthBlockHeader(
        bcos::bytesConstRef _data, bcos::Error::UniquePtr& error);
    static void calculateRLPHash(
        bcos::protocol::BlockHeader::Ptr header, bcos::Error::UniquePtr& error);

    const EthBlockHeaderData& data() const { return m_data; }

private:
    EthBlockHeaderData m_data;
};

}  // namespace bcos::protocol
