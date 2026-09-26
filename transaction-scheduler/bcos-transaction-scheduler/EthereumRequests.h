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
 * @file EthereumRequests.h
 * @brief EIP-7685 requestsHash assembly for the external-block verifier: the
 *        EIP-6110 deposit requests collected from the block's receipts plus the
 *        EIP-7002/7251 block-end system-call requests, hashed the way evmone's
 *        calculate_requests_hash does (sha256 over the concatenated per-request
 *        sha256 digests of the non-empty typed entries, in type order). Ported
 *        from evmone's eth/state/requests.cpp onto bcos receipts — no bcos-evm
 *        dependency, matching EthereumSystemCalls.h's porting contract.
 * @date 2026/9/22
 */
#pragma once

#include "ethereum-executor/EthSystemCalls.h"
#include <bcos-crypto/hash/Sha256.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <optional>
#include <vector>

namespace bcos::scheduler_v1
{
/// EIP-6110 deposit contract on Ethereum mainnet — the default for
/// EvmcForkTimestamps::depositContractAddress; per-chain override comes from
/// [ethereum] deposit_contract_address (config.ini). Same constant evmone pins
/// as DEPOSIT_CONTRACT_ADDRESS (bcos-evm requests.hpp), parameterized here.
inline const bcos::Address c_mainnetDepositContractAddress{
    std::string("0x00000000219ab540356cBB839Cbe05303d7705Fa"), bcos::Address::FromHex,
    bcos::Address::AlignRight};

/// The topic of the deposit contract's DepositEvent log (EIP-6110), same value as
/// evmone's DEPOSIT_EVENT_SIGNATURE_HASH.
inline const bcos::h256 c_depositEventSignatureHash{
    std::string("0x649bbc62d0e31342afea4e5cd82d4049e7e1ee912fc0889aa790803be39038c5"),
    bcos::h256::FromHex, bcos::h256::AlignRight};

/// EIP-6110 deposit collection (port of evmone's collect_deposit_requests): every
/// DepositEvent log emitted by the deposit contract, in receipt/log order, contributes
/// its 192-byte deposit request (pubkey48 ‖ withdrawal_credentials32 ‖ amount8 ‖
/// signature96 ‖ index8) to the returned byte string. Returns nullopt when a matching
/// log's ABI layout is malformed — per EIP-6110 block validity the block is then invalid.
inline std::optional<bcos::bytes> collectDepositRequestsData(
    std::vector<protocol::TransactionReceipt::Ptr> const& receipts,
    bcos::Address const& depositContractAddress)
{
    constexpr size_t WORD = 32;
    constexpr size_t DEPOSIT_LOG_DATA_SIZE = 576;
    // ABI layout of the DepositEvent log data: five head words (offsets), then each
    // dynamic bytes field as a length word followed by the value padded to words.
    constexpr size_t PUBKEY_OFFSET = WORD * 5;                 // 160
    constexpr size_t PUBKEY_SIZE = 48;
    constexpr size_t WITHDRAWAL_OFFSET = PUBKEY_OFFSET + WORD + WORD * 2;   // 256
    constexpr size_t WITHDRAWAL_SIZE = 32;
    constexpr size_t AMOUNT_OFFSET = WITHDRAWAL_OFFSET + WORD + WORD;       // 320
    constexpr size_t AMOUNT_SIZE = 8;
    constexpr size_t SIGNATURE_OFFSET = AMOUNT_OFFSET + WORD + WORD;        // 384
    constexpr size_t SIGNATURE_SIZE = 96;
    constexpr size_t INDEX_OFFSET = SIGNATURE_OFFSET + WORD + WORD * 3;     // 512
    constexpr size_t INDEX_SIZE = 8;
    constexpr std::array<size_t, 5> EXPECTED_OFFSETS{
        PUBKEY_OFFSET, WITHDRAWAL_OFFSET, AMOUNT_OFFSET, SIGNATURE_OFFSET, INDEX_OFFSET};

    // A 32-byte ABI word read as a size: nullopt when the value does not fit uint32.
    auto readWordAsSize = [](bcos::bytesConstRef data, size_t pos) -> std::optional<uint32_t> {
        for (size_t i = pos; i < pos + WORD - sizeof(uint32_t); ++i)
        {
            if (data[i] != 0)
            {
                return std::nullopt;
            }
        }
        uint32_t value = 0;
        for (size_t i = pos + WORD - sizeof(uint32_t); i < pos + WORD; ++i)
        {
            value = (value << 8) | data[i];
        }
        return value;
    };

    bcos::bytes depositRequests;
    auto const contractAddress = std::string_view(
        reinterpret_cast<const char*>(depositContractAddress.data()), bcos::Address::SIZE);
    for (auto const& receipt : receipts)
    {
        for (auto const& log : receipt->logEntries())
        {
            // EIP-6110 block-validity pseudocode: filter by contract address and topic0.
            if (log.address() != contractAddress)
            {
                continue;
            }
            if (log.topics().empty() || log.topics()[0] != c_depositEventSignatureHash)
            {
                continue;
            }
            auto const data = log.data();
            if (data.size() != DEPOSIT_LOG_DATA_SIZE)
            {
                return std::nullopt;
            }
            // The head offsets must point at the canonical field positions, and every
            // field's length word must match its fixed size — any deviation means the
            // log is not a deposit event's canonical encoding and collection fails.
            for (size_t i = 0; i < EXPECTED_OFFSETS.size(); ++i)
            {
                auto const offset = readWordAsSize(data, i * WORD);
                if (!offset || *offset != EXPECTED_OFFSETS[i])
                {
                    return std::nullopt;
                }
            }
            auto const fieldSizeOk = [&](size_t offset, uint32_t expected) {
                auto const size = readWordAsSize(data, offset);
                return size.has_value() && *size == expected;
            };
            if (!fieldSizeOk(PUBKEY_OFFSET, PUBKEY_SIZE) ||
                !fieldSizeOk(WITHDRAWAL_OFFSET, WITHDRAWAL_SIZE) ||
                !fieldSizeOk(AMOUNT_OFFSET, AMOUNT_SIZE) ||
                !fieldSizeOk(SIGNATURE_OFFSET, SIGNATURE_SIZE) ||
                !fieldSizeOk(INDEX_OFFSET, INDEX_SIZE))
            {
                return std::nullopt;
            }
            auto append = [&depositRequests](bcos::bytesConstRef data, size_t pos, size_t size) {
                depositRequests.insert(depositRequests.end(), data.data() + pos,
                    data.data() + pos + size);
            };
            append(data, PUBKEY_OFFSET + WORD, PUBKEY_SIZE);
            append(data, WITHDRAWAL_OFFSET + WORD, WITHDRAWAL_SIZE);
            append(data, AMOUNT_OFFSET + WORD, AMOUNT_SIZE);
            append(data, SIGNATURE_OFFSET + WORD, SIGNATURE_SIZE);
            append(data, INDEX_OFFSET + WORD, INDEX_SIZE);
        }
    }
    return depositRequests;
}

/// EIP-7685 requestsHash (port of evmone's calculate_requests_hash): for each request
/// type in [deposit, withdrawal, consolidation] order, sha256(type_byte ‖ requests_data)
/// of the non-empty entries, concatenated and sha256'd; the empty list hashes to
/// sha256("") — the canonical empty requestsHash every post-Prague header without
/// requests carries. `blockEndRequests` are the EIP-7002/7251 system-call outputs in
/// REQUESTS_SYSTEM_CONTRACTS (withdrawal, consolidation) order.
inline crypto::HashType calculateRequestsHash(
    bcos::bytes const& depositRequestsData,
    std::vector<executor_v1::eth::EthRequests> const& blockEndRequests)
{
    bcos::bytes digests;
    auto appendIfNonEmpty = [&digests](uint8_t type, bcos::bytesConstRef data) {
        if (data.empty())
        {
            return;
        }
        bcos::bytes entry;
        entry.reserve(1 + data.size());
        entry.push_back(type);
        entry.insert(entry.end(), data.begin(), data.end());
        auto const digest = bcos::crypto::sha256Hash(bcos::ref(entry));
        digests.insert(digests.end(), digest.begin(), digest.end());
    };
    appendIfNonEmpty(static_cast<uint8_t>(executor_v1::eth::EthRequests::Type::deposit),
        bcos::ref(depositRequestsData));
    for (auto const& request : blockEndRequests)
    {
        auto const data = request.data();
        appendIfNonEmpty(static_cast<uint8_t>(request.type()),
            bcos::bytesConstRef(data.data(), data.size()));
    }
    return bcos::crypto::sha256Hash(bcos::ref(digests));
}
}  // namespace bcos::scheduler_v1
