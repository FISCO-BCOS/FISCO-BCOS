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
 * @file OpEnvelopeMirrorTest.cpp
 * @brief OP signed-envelope mirror decoding and unbound-mirror gate tests
 */

// The signed envelope is authoritative over the tars mirror. Tests call the gate helpers
// directly with independent envelope bytes and mirror fields.

#include <opstack-executor/OpDepositEncode.h>
#include <opstack-executor/OpstackExecutor.h>

#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-utilities/Common.h>
#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <optional>
#include <string>

using bcos::executor_v1::eth::toEvmoneTransaction;
using bcos::executor_v1::opstack::blockPathUnboundAuthorizationList;
using bcos::executor_v1::opstack::blockPathZeroSender;
using bcos::executor_v1::opstack::envelopeChainIdMismatch;
using bcos::executor_v1::opstack::envelopeExecutionFieldsMismatch;
namespace rlp = bcos::codec::rlp;

namespace
{
class FakeTx : public bcos::protocol::Transaction
{
public:
    uint8_t m_kind = 2;
    bcos::bytes m_input;
    int64_t m_gasLimit = 5000000;
    std::optional<bcos::u256> m_gasPrice;
    std::optional<bcos::u256> m_maxFeePerGas;
    std::optional<bcos::u256> m_maxPriorityFeePerGas;
    std::optional<bcos::u256> m_maxFeePerBlobGas;
    std::string m_sender = std::string(sizeof(evmc_address), '\xaa');
    std::string m_to;  // hex form ("0x" + 40 hex chars), or empty = creation
    bcos::u256 m_value = 0;
    std::string m_chainId = "10";  // DECIMAL string (mirror)
    std::string m_nonce = "0x7";   // hex quantity
    bcos::bytes m_extraBytes;
    std::optional<uint64_t> m_reportedEnvelopeChainId;
    bcos::protocol::Web3AccessList m_accessList;
    bcos::protocol::VersionedHashes m_blobHashes;

    uint8_t web3TypedTxKind() const override { return m_kind; }
    bcos::bytesConstRef input() const override
    {
        return bcos::bytesConstRef{m_input.data(), m_input.size()};
    }
    int64_t gasLimit() const override { return m_gasLimit; }
    std::optional<bcos::u256> gasPrice() const override { return m_gasPrice; }
    std::optional<bcos::u256> maxFeePerGas() const override { return m_maxFeePerGas; }
    std::optional<bcos::u256> maxPriorityFeePerGas() const override
    {
        return m_maxPriorityFeePerGas;
    }
    std::optional<bcos::u256> maxFeePerBlobGas() const override { return m_maxFeePerBlobGas; }
    std::string_view sender() const override { return m_sender; }
    std::string_view to() const override { return m_to; }
    bcos::u256 value() const override { return m_value; }
    std::string_view chainId() const override { return m_chainId; }
    std::string_view nonce() const override { return m_nonce; }
    bcos::bytesConstRef extraTransactionBytes() const override
    {
        return bcos::bytesConstRef{m_extraBytes.data(), m_extraBytes.size()};
    }
    std::optional<uint64_t> web3ChainIdFromEnvelope() const override
    {
        if (m_reportedEnvelopeChainId.has_value())
            return m_reportedEnvelopeChainId;
        return bcos::rlp::protocol::web3ChainIdFromEnvelope(extraTransactionBytes());
    }
    bcos::protocol::Web3AccessList web3AccessList() const override { return m_accessList; }
    bcos::protocol::VersionedHashes blobVersionedHashes() const override { return m_blobHashes; }

    // ---- unused stubs ----
    void decode(bcos::bytesConstRef) override {}
    void encode(bcos::bytes&) const override {}
    bcos::crypto::HashType hash() const override { return {}; }
    int32_t version() const override { return 0; }
    std::string_view groupId() const override { return {}; }
    int64_t blockLimit() const override { return 0; }
    void setNonce(std::string) override {}
    std::string_view abi() const override { return {}; }
    bcos::bytesConstRef extension() const override { return {}; }
    std::string_view extraData() const override { return {}; }
    int64_t importTime() const override { return 0; }
    void setImportTime(int64_t) override {}
    uint8_t type() const override { return 1; }  // Web3Transaction
    void forceSender(const bcos::bytes&) override {}
    void clearSenderAndHash() override {}
    void calculateHash(const bcos::crypto::Hash&) override {}
    bcos::bytesConstRef signatureData() const override { return {}; }
    int32_t attribute() const override { return 0; }
    void setAttribute(int32_t) override {}
};

/// The cross-check compares the envelope against the values the executor will actually use
/// (evmTx, built by toEvmoneTransaction from the mirror) — never the raw mirror strings.
/// Unset fee mirrors are filled to match the envelope helpers in this file; a test
/// that forges a fee must set the corresponding FakeTx field first.
evmone::state::Transaction evmTxOf(FakeTx& tx)
{
    constexpr uint64_t kEip1559Fee = 30'000'000'000ULL;
    constexpr uint64_t kLegacyGasPrice = 1'000'000'000ULL;
    if (tx.m_kind == 0 || tx.m_kind == 1)
    {
        if (!tx.m_gasPrice.has_value())
        {
            tx.m_gasPrice = kLegacyGasPrice;
        }
    }
    else
    {
        if (!tx.m_maxFeePerGas.has_value())
        {
            tx.m_maxFeePerGas = kEip1559Fee;
        }
        if (!tx.m_maxPriorityFeePerGas.has_value())
        {
            tx.m_maxPriorityFeePerGas = kEip1559Fee;
        }
        if (tx.m_kind == 3 && !tx.m_maxFeePerBlobGas.has_value())
        {
            tx.m_maxFeePerBlobGas = 1;
        }
    }
    return toEvmoneTransaction(tx);
}

/// Build a canonical EIP-1559 (0x02) envelope: 0x02 || rlp([chainId, nonce, prio, maxFee,
/// gasLimit, to, value, data, accessList]).
bcos::bytes eip1559Envelope(uint64_t chainId, uint64_t nonce, uint64_t gasLimit,
    std::string_view toHex, bcos::u256 value, bcos::bytes const& data, bool toIsList = false,
    bool dataIsList = false, bool nonceIsList = false, bool gasIsList = false,
    bool valueIsList = false)
{
    auto item = [](bcos::bytes const& payload) {
        bcos::bytes out;
        rlp::encode(out, bcos::bytesConstRef{payload.data(), payload.size()});
        return out;
    };
    auto intItem = [](uint64_t v) {
        bcos::bytes out;
        rlp::encode(out, v);
        return out;
    };
    // 1-byte list 0xc1 0x05: payloadLength==1 passes the integer width guard, so the kind
    // check (not decode's UnexpectedList) is what must reject it.
    auto shortList = []() -> bcos::bytes { return {0xc1, 0x05}; };
    bcos::bytes payload;
    auto append = [&payload](
                      bcos::bytes const& b) { payload.insert(payload.end(), b.begin(), b.end()); };
    append(intItem(chainId));
    if (nonceIsList)
        append(shortList());
    else
        append(intItem(nonce));
    append(intItem(30000000000));
    append(intItem(30000000000));
    if (gasIsList)
        append(shortList());
    else
        append(intItem(gasLimit));
    auto toBytes = bcos::fromHex(toHex.substr(2));
    if (toIsList)
        payload.push_back(0xc0);
    else
        append(item(toBytes));
    // Test values are small; encode the u256 as an RLP integer via its low 64 bits.
    if (valueIsList)
        append(shortList());
    else
        append(intItem(static_cast<uint64_t>(value)));
    if (dataIsList)
        payload.push_back(0xc0);
    else
        append(item(data));
    payload.push_back(0xc0);  // empty accessList

    bcos::bytes out{static_cast<bcos::byte>(0x02)};
    rlp::encodeHeader(out, {.isList = true, .payloadLength = payload.size()});
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

/// Build a legacy envelope: bare RLP list rlp([nonce, gasPrice, gasLimit, to, value, data, v, r,
/// s]) (no EIP-2718 type byte; first byte is the list header, so isTypedWeb3Envelope is false).
bcos::bytes legacyEnvelope(uint64_t nonce, uint64_t gasLimit, std::string_view toHex,
    bcos::u256 value, bcos::bytes const& data, bool nonceIsList = false, bool gasIsList = false,
    bool valueIsList = false)
{
    auto item = [](bcos::bytes const& payload) {
        bcos::bytes out;
        rlp::encode(out, bcos::bytesConstRef{payload.data(), payload.size()});
        return out;
    };
    auto intItem = [](uint64_t v) {
        bcos::bytes out;
        rlp::encode(out, v);
        return out;
    };
    auto shortList = []() -> bcos::bytes { return {0xc1, 0x05}; };
    bcos::bytes payload;
    auto append = [&payload](
                      bcos::bytes const& b) { payload.insert(payload.end(), b.begin(), b.end()); };
    if (nonceIsList)
        append(shortList());
    else
        append(intItem(nonce));
    append(intItem(1000000000));  // gasPrice
    if (gasIsList)
        append(shortList());
    else
        append(intItem(gasLimit));
    auto toBytes = bcos::fromHex(toHex.substr(2));
    append(item(toBytes));
    if (valueIsList)
        append(shortList());
    else
        append(intItem(static_cast<uint64_t>(value)));
    append(item(data));
    append(intItem(27));  // v (unprotected legacy)
    append(intItem(1));   // non-empty r: full signed-envelope shape, not an EIP-155 preimage
    append(intItem(2));   // non-empty s

    bcos::bytes out;
    rlp::encodeHeader(out, {.isList = true, .payloadLength = payload.size()});
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

/// Build an EIP-2930 (0x01) envelope: 0x01 || rlp([chainId, nonce, gasPrice, gasLimit, to, value,
/// data, accessList]).
bcos::bytes accessListEnvelope(uint64_t chainId, uint64_t nonce, uint64_t gasLimit,
    std::string_view toHex, bcos::u256 value, bcos::bytes const& data, bool nonceIsList = false,
    bool gasIsList = false, bool valueIsList = false)
{
    auto item = [](bcos::bytes const& payload) {
        bcos::bytes out;
        rlp::encode(out, bcos::bytesConstRef{payload.data(), payload.size()});
        return out;
    };
    auto intItem = [](uint64_t v) {
        bcos::bytes out;
        rlp::encode(out, v);
        return out;
    };
    auto shortList = []() -> bcos::bytes { return {0xc1, 0x05}; };
    bcos::bytes payload;
    auto append = [&payload](
                      bcos::bytes const& b) { payload.insert(payload.end(), b.begin(), b.end()); };
    append(intItem(chainId));
    if (nonceIsList)
        append(shortList());
    else
        append(intItem(nonce));
    append(intItem(1000000000));  // gasPrice
    if (gasIsList)
        append(shortList());
    else
        append(intItem(gasLimit));
    auto toBytes = bcos::fromHex(toHex.substr(2));
    append(item(toBytes));
    if (valueIsList)
        append(shortList());
    else
        append(intItem(static_cast<uint64_t>(value)));
    append(item(data));
    payload.push_back(0xc0);  // empty accessList

    bcos::bytes out{static_cast<bcos::byte>(0x01)};
    rlp::encodeHeader(out, {.isList = true, .payloadLength = payload.size()});
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

/// Build a 0x03 (blob) or 0x04 (7702) envelope. The first 8 fields are identical to 0x02
/// (chainId, nonce, prio, maxFee, gasLimit, to, value, data), followed by accessList and a
/// real per-type tail. The gate reads only through data, so both shapes exercise the shared
/// typed (envelopeKind != 0x01) index layout [1,4,5,6,7].
bcos::bytes blobOrAuthEnvelope(uint8_t typeByte, uint64_t chainId, uint64_t nonce,
    uint64_t gasLimit, std::string_view toHex, bcos::u256 value, bcos::bytes const& data)
{
    auto item = [](bcos::bytes const& payload) {
        bcos::bytes out;
        rlp::encode(out, bcos::bytesConstRef{payload.data(), payload.size()});
        return out;
    };
    auto intItem = [](uint64_t v) {
        bcos::bytes out;
        rlp::encode(out, v);
        return out;
    };
    bcos::bytes payload;
    auto append = [&payload](
                      bcos::bytes const& b) { payload.insert(payload.end(), b.begin(), b.end()); };
    append(intItem(chainId));
    append(intItem(nonce));
    append(intItem(30000000000));  // maxPriorityFeePerGas
    append(intItem(30000000000));  // maxFeePerGas
    append(intItem(gasLimit));
    auto toBytes = bcos::fromHex(toHex.substr(2));
    append(item(toBytes));
    append(intItem(static_cast<uint64_t>(value)));
    append(item(data));
    payload.push_back(0xc0);  // empty accessList
    if (typeByte == 0x03)
    {
        append(intItem(1));       // maxFeePerBlobGas
        payload.push_back(0xc0);  // empty blobVersionedHashes
    }
    else
    {
        payload.push_back(0xc0);  // empty authorizationList
    }

    bcos::bytes out{static_cast<bcos::byte>(typeByte)};
    rlp::encodeHeader(out, {.isList = true, .payloadLength = payload.size()});
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

/// RLP-encode an EIP-2930 access list: [[address20, [keys32...]], ...].
bcos::bytes rlpAccessList(bcos::protocol::Web3AccessList const& list)
{
    auto stringItem = [](bcos::bytes const& payload) {
        bcos::bytes out;
        rlp::encode(out, bcos::bytesConstRef{payload.data(), payload.size()});
        return out;
    };
    bcos::bytes entries;
    for (auto const& entry : list)
    {
        bcos::bytes entryPayload = stringItem(
            bcos::bytes(entry.account.data(), entry.account.data() + entry.account.size()));
        bcos::bytes keysPayload;
        for (auto const& key : entry.storageKeys)
        {
            auto encoded = stringItem(bcos::bytes(key.data(), key.data() + key.size()));
            keysPayload.insert(keysPayload.end(), encoded.begin(), encoded.end());
        }
        bcos::bytes keys;
        rlp::encodeHeader(keys, {.isList = true, .payloadLength = keysPayload.size()});
        keys.insert(keys.end(), keysPayload.begin(), keysPayload.end());
        entryPayload.insert(entryPayload.end(), keys.begin(), keys.end());
        bcos::bytes entryRlp;
        rlp::encodeHeader(entryRlp, {.isList = true, .payloadLength = entryPayload.size()});
        entryRlp.insert(entryRlp.end(), entryPayload.begin(), entryPayload.end());
        entries.insert(entries.end(), entryRlp.begin(), entryRlp.end());
    }
    bcos::bytes out;
    rlp::encodeHeader(out, {.isList = true, .payloadLength = entries.size()});
    out.insert(out.end(), entries.begin(), entries.end());
    return out;
}

/// RLP-encode a list of 32-byte versioned hashes.
bcos::bytes rlpHashList(std::vector<bcos::h256> const& hashes)
{
    bcos::bytes payload;
    for (auto const& hash : hashes)
    {
        bcos::bytes encoded;
        rlp::encode(encoded, bcos::bytesConstRef{hash.data(), hash.size()});
        payload.insert(payload.end(), encoded.begin(), encoded.end());
    }
    bcos::bytes out;
    rlp::encodeHeader(out, {.isList = true, .payloadLength = payload.size()});
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

/// EIP-1559 (0x02) envelope carrying a NON-EMPTY accessList (12-item shape, no 4844 fields).
bcos::bytes eip1559EnvelopeWithAccessList(uint64_t chainId, uint64_t nonce, uint64_t gasLimit,
    std::string_view toHex, bcos::u256 value, bcos::bytes const& data,
    bcos::protocol::Web3AccessList const& accessList)
{
    auto item = [](bcos::bytes const& payload) {
        bcos::bytes out;
        rlp::encode(out, bcos::bytesConstRef{payload.data(), payload.size()});
        return out;
    };
    auto intItem = [](uint64_t v) {
        bcos::bytes out;
        rlp::encode(out, v);
        return out;
    };
    bcos::bytes payload;
    auto append = [&payload](
                      bcos::bytes const& b) { payload.insert(payload.end(), b.begin(), b.end()); };
    append(intItem(chainId));
    append(intItem(nonce));
    append(intItem(30000000000));
    append(intItem(30000000000));
    append(intItem(gasLimit));
    auto toBytes = bcos::fromHex(toHex.substr(2));
    append(item(toBytes));
    append(intItem(static_cast<uint64_t>(value)));
    append(item(data));
    append(rlpAccessList(accessList));

    bcos::bytes out{static_cast<bcos::byte>(0x02)};
    rlp::encodeHeader(out, {.isList = true, .payloadLength = payload.size()});
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

/// 0x03 blob envelope with explicit accessList and blobVersionedHashes (14-item shape).
bcos::bytes blobEnvelopeWithHashes(uint64_t chainId, uint64_t nonce, uint64_t gasLimit,
    std::string_view toHex, bcos::u256 value, bcos::bytes const& data,
    bcos::protocol::Web3AccessList const& accessList, std::vector<bcos::h256> const& blobHashes)
{
    auto item = [](bcos::bytes const& payload) {
        bcos::bytes out;
        rlp::encode(out, bcos::bytesConstRef{payload.data(), payload.size()});
        return out;
    };
    auto intItem = [](uint64_t v) {
        bcos::bytes out;
        rlp::encode(out, v);
        return out;
    };
    bcos::bytes payload;
    auto append = [&payload](
                      bcos::bytes const& b) { payload.insert(payload.end(), b.begin(), b.end()); };
    append(intItem(chainId));
    append(intItem(nonce));
    append(intItem(30000000000));
    append(intItem(30000000000));
    append(intItem(gasLimit));
    auto toBytes = bcos::fromHex(toHex.substr(2));
    append(item(toBytes));
    append(intItem(static_cast<uint64_t>(value)));
    append(item(data));
    append(rlpAccessList(accessList));
    append(intItem(1));  // maxFeePerBlobGas
    append(rlpHashList(blobHashes));
    append(intItem(0));  // yParity
    append(intItem(1));  // r
    append(intItem(1));  // s

    bcos::bytes out{static_cast<bcos::byte>(0x03)};
    rlp::encodeHeader(out, {.isList = true, .payloadLength = payload.size()});
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}
/// 0x02 envelope carrying the EIP-4844-in-1559 extension (14-item shape): maxFeePerBlobGas
/// at idx 9 and blobVersionedHashes at idx 10, then yParity/r/s.
bcos::bytes eip1559EnvelopeWithBlobs(uint64_t chainId, uint64_t nonce, uint64_t gasLimit,
    std::string_view toHex, bcos::u256 value, bcos::bytes const& data)
{
    auto item = [](bcos::bytes const& payload) {
        bcos::bytes out;
        rlp::encode(out, bcos::bytesConstRef{payload.data(), payload.size()});
        return out;
    };
    auto intItem = [](uint64_t v) {
        bcos::bytes out;
        rlp::encode(out, v);
        return out;
    };
    bcos::bytes payload;
    auto append = [&payload](
                      bcos::bytes const& b) { payload.insert(payload.end(), b.begin(), b.end()); };
    append(intItem(chainId));
    append(intItem(nonce));
    append(intItem(30000000000));
    append(intItem(30000000000));
    append(intItem(gasLimit));
    auto toBytes = bcos::fromHex(toHex.substr(2));
    append(item(toBytes));
    append(intItem(static_cast<uint64_t>(value)));
    append(item(data));
    payload.push_back(0xc0);  // empty accessList
    append(intItem(1));       // maxFeePerBlobGas (idx 9)
    payload.push_back(0xc0);  // empty blobVersionedHashes (idx 10)
    append(intItem(0));       // yParity
    append(intItem(1));       // r
    append(intItem(1));       // s

    bcos::bytes out{static_cast<bcos::byte>(0x02)};
    rlp::encodeHeader(out, {.isList = true, .payloadLength = payload.size()});
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OpEnvelopeMirrorSuite)

// The chainId gate reads the ENVELOPE, never the mirror: mirror says "10" but the envelope
// chainId is 9 — must reject with the pinned message, so deleting the gate fails the test.
BOOST_AUTO_TEST_CASE(EnvelopeChainIdWinsOverMirror)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(
        9, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {});
    tx.m_chainId = "10";  // mirror claims the node chainId
    auto const gate = envelopeChainIdMismatch(tx, 10);
    BOOST_REQUIRE(gate.has_value());
    BOOST_CHECK(std::string(*gate).find("envelope chain_id 9 does not match node chainId 10") !=
                std::string::npos);
}

// a legacy full envelope with malformed v (0/1, 29-34) must NOT be treated as an
// unprotected-legacy exemption — it fails closed. op-geth's EIP-155 signer rejects such v; the
// gate must not let a malformed-signature tx execute as "pre-EIP-155".
BOOST_AUTO_TEST_CASE(MalformedLegacyVRejectedByChainIdGate)
{
    // Reuse the legacyEnvelope shape (6 fields + v, r, s) with each malformed v value.
    for (uint64_t badV : {0u, 1u, 29u, 30u, 34u})
    {
        FakeTx tx;
        tx.m_kind = 0;
        tx.m_extraBytes = legacyEnvelope(
            7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {});
        // Patch the v field (field index 6, the 7th item) to the malformed value.
        auto env = tx.m_extraBytes;
        // RLP list header: single-byte 0xc0|len for <56 payloads; patch by rebuilding via the
        // raw field walk is overkill — the classifier reads v from field 7, so rewrite the
        // envelope with the v we want using the same builder shape as legacyEnvelope.
        auto item = [](bcos::bytes const& payload) {
            bcos::bytes out;
            rlp::encode(out, bcos::bytesConstRef{payload.data(), payload.size()});
            return out;
        };
        auto intItem = [](uint64_t v) {
            bcos::bytes out;
            rlp::encode(out, v);
            return out;
        };
        bcos::bytes payload;
        auto append = [&payload](bcos::bytes const& b) {
            payload.insert(payload.end(), b.begin(), b.end());
        };
        append(intItem(7));
        append(intItem(1000000000));  // gasPrice
        append(intItem(5000000));     // gasLimit
        auto toBytes = bcos::fromHex("811a752c8cd697e3cb27279c330ed1ada745a8d7");
        append(item(toBytes));
        append(intItem(5));
        append(item({}));
        append(intItem(badV));  // v
        append(intItem(1));     // r (non-empty: full signed-envelope shape)
        append(intItem(2));     // s
        bcos::bytes rebuilt;
        rlp::encodeHeader(rebuilt, {.isList = true, .payloadLength = payload.size()});
        rebuilt.insert(rebuilt.end(), payload.begin(), payload.end());
        tx.m_extraBytes = rebuilt;
        auto const gate = envelopeChainIdMismatch(tx, 10);
        BOOST_CHECK_MESSAGE(gate.has_value(), "malformed v=" << badV << " must fail closed");
        if (gate.has_value())
        {
            BOOST_CHECK(std::string(*gate).find("malformed chainId/v") != std::string::npos);
        }
    }
}

// the two legitimate unprotected forms (6-field preimage, and full envelope with
// v=27/28) remain exempt; a full envelope with v>=35 is protected and must match the node.
BOOST_AUTO_TEST_CASE(ValidLegacyVFormsPassChainIdGate)
{
    // 6-field preimage (no v/r/s) → unprotected exemption.
    {
        FakeTx tx;
        tx.m_kind = 0;
        // Build a 6-field legacy list.
        auto item = [](bcos::bytes const& payload) {
            bcos::bytes out;
            rlp::encode(out, bcos::bytesConstRef{payload.data(), payload.size()});
            return out;
        };
        auto intItem = [](uint64_t v) {
            bcos::bytes out;
            rlp::encode(out, v);
            return out;
        };
        bcos::bytes payload;
        auto append = [&payload](bcos::bytes const& b) {
            payload.insert(payload.end(), b.begin(), b.end());
        };
        append(intItem(7));
        append(intItem(1000000000));  // gasPrice
        append(intItem(5000000));     // gasLimit
        auto toBytes = bcos::fromHex("811a752c8cd697e3cb27279c330ed1ada745a8d7");
        append(item(toBytes));
        append(intItem(5));
        append(item({}));
        bcos::bytes preimage;
        rlp::encodeHeader(preimage, {.isList = true, .payloadLength = payload.size()});
        preimage.insert(preimage.end(), payload.begin(), payload.end());
        tx.m_extraBytes = preimage;
        BOOST_CHECK(!envelopeChainIdMismatch(tx, 10).has_value());
    }
    // Full envelope v=27/28 → unprotected exemption. legacyEnvelope hardcodes v=27, so the
    // v=28 arm is rebuilt explicitly with v=28 (both must pass the gate).
    auto buildLegacyWithV = [&](uint64_t v) {
        auto item = [](bcos::bytes const& payload) {
            bcos::bytes out;
            rlp::encode(out, bcos::bytesConstRef{payload.data(), payload.size()});
            return out;
        };
        auto intItem = [](uint64_t val) {
            bcos::bytes out;
            rlp::encode(out, val);
            return out;
        };
        bcos::bytes payload;
        auto append = [&payload](bcos::bytes const& b) {
            payload.insert(payload.end(), b.begin(), b.end());
        };
        append(intItem(7));
        append(intItem(1000000000));  // gasPrice
        append(intItem(5000000));     // gasLimit
        auto toBytes = bcos::fromHex("811a752c8cd697e3cb27279c330ed1ada745a8d7");
        append(item(toBytes));
        append(intItem(5));
        append(item({}));
        append(intItem(v));  // v
        append(intItem(1));  // r
        append(intItem(2));  // s
        bcos::bytes out;
        rlp::encodeHeader(out, {.isList = true, .payloadLength = payload.size()});
        out.insert(out.end(), payload.begin(), payload.end());
        return out;
    };
    for (uint64_t okV : {27u, 28u})
    {
        FakeTx tx;
        tx.m_kind = 0;
        tx.m_extraBytes = buildLegacyWithV(okV);
        BOOST_CHECK(!envelopeChainIdMismatch(tx, 10).has_value());
    }
    // Full envelope v=37 → chainId 1; node chainId 1 passes, node chainId 10 fails.
    {
        FakeTx tx;
        tx.m_kind = 0;
        auto item = [](bcos::bytes const& payload) {
            bcos::bytes out;
            rlp::encode(out, bcos::bytesConstRef{payload.data(), payload.size()});
            return out;
        };
        auto intItem = [](uint64_t v) {
            bcos::bytes out;
            rlp::encode(out, v);
            return out;
        };
        bcos::bytes payload;
        auto append = [&payload](bcos::bytes const& b) {
            payload.insert(payload.end(), b.begin(), b.end());
        };
        append(intItem(7));
        append(intItem(1000000000));
        append(intItem(5000000));
        auto toBytes = bcos::fromHex("811a752c8cd697e3cb27279c330ed1ada745a8d7");
        append(item(toBytes));
        append(intItem(5));
        append(item({}));
        append(intItem(37));  // v → chainId (37-35)>>1 = 1
        append(intItem(1));
        append(intItem(2));
        bcos::bytes protectedEnv;
        rlp::encodeHeader(protectedEnv, {.isList = true, .payloadLength = payload.size()});
        protectedEnv.insert(protectedEnv.end(), payload.begin(), payload.end());
        tx.m_extraBytes = protectedEnv;
        BOOST_CHECK(!envelopeChainIdMismatch(tx, 1).has_value());
        BOOST_CHECK(envelopeChainIdMismatch(tx, 10).has_value());
    }
}

// Matching envelope chainId passes the gate even when the mirror differs.
BOOST_AUTO_TEST_CASE(EnvelopeChainIdMatchesNode)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(
        10, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {});
    tx.m_chainId = "999";  // mirror is wrong but irrelevant
    BOOST_CHECK(!envelopeChainIdMismatch(tx, 10).has_value());
}

// The convenience overload must derive chainId from the same envelope-byte parser as the block
// path. A polymorphic override must not create a second consensus interpretation.
BOOST_AUTO_TEST_CASE(EnvelopeChainIdGateIgnoresDivergentVirtualOverride)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(
        9, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {});
    tx.m_reportedEnvelopeChainId = 10;
    auto const gate = envelopeChainIdMismatch(tx, 10);
    BOOST_REQUIRE(gate.has_value());
    BOOST_CHECK_EQUAL(*gate, "tx envelope chain_id 9 does not match node chainId 10");
}

// Typed envelopes must carry a parseable chainId in field 0. A type byte followed by a malformed
// list is not a legacy/pre-EIP-155 exemption and must fail closed.
BOOST_AUTO_TEST_CASE(TypedEnvelopeWithoutParseableChainIdRejected)
{
    FakeTx tx;
    tx.m_extraBytes = {0x02, 0xc0};
    auto const gate = envelopeChainIdMismatch(tx, 10);
    BOOST_REQUIRE(gate.has_value());
    BOOST_CHECK_EQUAL(*gate, "typed tx envelope is missing a parseable chainId");
}

// 0x7E deposit envelopes are typed-looking (type byte < 0x80) but carry no chainId: field 0 is
// sourceHash. The skip is unconditional (field 0 is never a chainId), so a deposit must pass
// the gate exactly where a 0x02 with an unparseable field 0 is rejected — deleting the skip
// branch fails this test.
BOOST_AUTO_TEST_CASE(DepositEnvelopeSkipsChainIdGate)
{
    FakeTx tx;
    bcos::evm::opstack::DepositTx dep{};
    std::fill(std::begin(dep.source_hash.bytes), std::end(dep.source_hash.bytes), 0x11);
    std::fill(std::begin(dep.from.bytes), std::end(dep.from.bytes), 0x22);
    dep.to = std::nullopt;
    dep.value = 0;
    dep.gas_limit = 100000;
    tx.m_extraBytes = bcos::evm::opstack::encodeDepositEnvelope(dep);
    BOOST_REQUIRE(!tx.m_extraBytes.empty());
    BOOST_CHECK_EQUAL(tx.m_extraBytes.front(), 0x7e);
    BOOST_CHECK(!envelopeChainIdMismatch(tx, 10).has_value());
}

// A forged mirror value must be rejected by the execution-fields cross-check.
BOOST_AUTO_TEST_CASE(MirrorValueDivergenceRejected)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(
        10, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {});
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_value = bcos::u256{999};  // mirror forged (everything else consistent)
    auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK(std::string(*mismatch).find("value mismatch") != std::string::npos);
}

// a forged mirror nonce must be rejected with the nonce-specific message (not just
// any mismatch) — the gate must compare nonce, not only to/value/data.
BOOST_AUTO_TEST_CASE(MirrorNonceDivergenceRejected)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(
        10, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {});
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x8";  // mirror forged (envelope says nonce 7)
    tx.m_gasLimit = 5000000;
    tx.m_value = bcos::u256{5};  // mirror value must match the envelope value
    auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK(std::string(*mismatch).find("nonce mismatch") != std::string::npos);
}

// a forged mirror gasLimit must be rejected with the gasLimit-specific message.
BOOST_AUTO_TEST_CASE(MirrorGasLimitDivergenceRejected)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(
        10, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {});
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000001;     // mirror forged (envelope says 5000000)
    tx.m_value = bcos::u256{5};  // mirror value must match the envelope value
    auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK(std::string(*mismatch).find("gasLimit mismatch") != std::string::npos);
}

// forged mirror fee fields must be rejected with the fee-specific message.
BOOST_AUTO_TEST_CASE(MirrorMaxFeePerGasDivergenceRejected)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(
        10, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {});
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_value = bcos::u256{5};
    tx.m_maxFeePerGas = bcos::u256{30'000'000'001ULL};  // envelope says 30 gwei
    tx.m_maxPriorityFeePerGas = bcos::u256{30'000'000'000ULL};
    auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK(std::string(*mismatch).find("maxFeePerGas mismatch") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(MirrorMaxPriorityFeePerGasDivergenceRejected)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(
        10, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {});
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_value = bcos::u256{5};
    tx.m_maxFeePerGas = bcos::u256{30'000'000'000ULL};
    tx.m_maxPriorityFeePerGas = bcos::u256{1};
    auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK(std::string(*mismatch).find("maxPriorityFeePerGas mismatch") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(MirrorGasPriceDivergenceRejected)
{
    FakeTx tx;
    tx.m_kind = 0;
    tx.m_extraBytes =
        legacyEnvelope(7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {});
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_value = bcos::u256{5};
    tx.m_gasPrice = bcos::u256{1'000'000'001ULL};  // envelope says 1 gwei
    auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK(std::string(*mismatch).find("gasPrice mismatch") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(MirrorMaxFeePerBlobGasDivergenceRejected)
{
    FakeTx tx;
    tx.m_kind = 3;
    tx.m_extraBytes = blobOrAuthEnvelope(
        0x03, 10, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {});
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_value = bcos::u256{5};
    tx.m_maxFeePerGas = bcos::u256{30'000'000'000ULL};
    tx.m_maxPriorityFeePerGas = bcos::u256{30'000'000'000ULL};
    tx.m_maxFeePerBlobGas = bcos::u256{2};  // envelope says 1
    auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK(std::string(*mismatch).find("maxFeePerBlobGas mismatch") != std::string::npos);
}

// The 0x02 4844 extension carries maxFeePerBlobGas at idx 9 too; a forged mirror value on
// that arm must be rejected like the 0x03 arm, or the envelope's signed blob-fee cap is
// never compared on a 14-item 0x02 tx.
BOOST_AUTO_TEST_CASE(MirrorMaxFeePerBlobGasOnEip1559ExtensionRejected)
{
    FakeTx tx;
    tx.m_kind = 2;
    tx.m_extraBytes = eip1559EnvelopeWithBlobs(
        10, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {});
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_value = bcos::u256{5};
    tx.m_maxFeePerGas = bcos::u256{30'000'000'000ULL};
    tx.m_maxPriorityFeePerGas = bcos::u256{30'000'000'000ULL};
    tx.m_maxFeePerBlobGas = bcos::u256{2};  // envelope says 1
    auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK(std::string(*mismatch).find("maxFeePerBlobGas mismatch") != std::string::npos);
}

// a forged mirror data must be rejected with the data-specific message. The FakeTx
// input() is the mirror's data; the envelope carries empty data, so any non-empty mirror data is
// a divergence.
BOOST_AUTO_TEST_CASE(MirrorDataDivergenceRejected)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(
        10, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {});
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_value = bcos::u256{5};  // mirror value must match the envelope value
    tx.m_input = {0xde, 0xad};   // mirror forged (envelope data is empty)
    auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK(std::string(*mismatch).find("data mismatch") != std::string::npos);
}

// a non-canonical RLP integer in the envelope (leading-zero multi-byte nonce) must
// be rejected by the canonicality gate, matching the deposit decoder's treatment. The 0x02
// envelope's nonce is encoded as 0x82 0x00 0x07 (2-byte payload with a leading zero) instead of
// the canonical bare byte 0x07.
BOOST_AUTO_TEST_CASE(NonCanonicalEnvelopeIntegerRejected)
{
    // Hand-build a 0x02 envelope whose nonce item is 0x82 0x00 0x07 (non-canonical).
    auto item = [](bcos::bytes const& payload) {
        bcos::bytes out;
        rlp::encode(out, bcos::bytesConstRef{payload.data(), payload.size()});
        return out;
    };
    auto intItem = [](uint64_t v) {
        bcos::bytes out;
        rlp::encode(out, v);
        return out;
    };
    bcos::bytes payload;
    auto append = [&payload](
                      bcos::bytes const& b) { payload.insert(payload.end(), b.begin(), b.end()); };
    append(intItem(10));         // chainId
    append({0x82, 0x00, 0x07});  // nonce: leading-zero 2-byte encoding of 7 (non-canonical)
    append(intItem(10));         // maxPriorityFeePerGas
    append(intItem(100));        // maxFeePerGas
    append(intItem(5000000));    // gasLimit
    auto toBytes = bcos::fromHex("811a752c8cd697e3cb27279c330ed1ada745a8d7");
    append(item(toBytes));
    append(intItem(5));       // value
    append(item({}));         // data
    payload.push_back(0xc0);  // empty accessList

    bcos::bytes out{static_cast<bcos::byte>(0x02)};
    rlp::encodeHeader(out, {.isList = true, .payloadLength = payload.size()});
    out.insert(out.end(), payload.begin(), payload.end());

    FakeTx tx;
    tx.m_extraBytes = out;
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x7";  // mirror value is the canonical 7
    tx.m_gasLimit = 5000000;
    auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK(
        std::string(*mismatch).find("nonce is not a canonical integer") != std::string::npos);
}

// A forged mirror `to` must be rejected.
BOOST_AUTO_TEST_CASE(MirrorToDivergenceRejected)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(
        10, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {});
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d8";  // last byte flipped
    BOOST_CHECK(envelopeExecutionFieldsMismatch(tx, evmTxOf(tx)).has_value());
}

// A forged mirror tx TYPE must be rejected: 0x02 envelope but mirror kind=0 (legacy) —
// the field indices would coincidentally align, yet execution would use legacy fee
// semantics and the receipts-root leaf would diverge between the two block paths.
BOOST_AUTO_TEST_CASE(MirrorKindDivergenceRejected)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(
        10, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {});
    tx.m_kind = 0;  // mirror claims legacy
    auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK(std::string(*mismatch).find("tx type mismatch") != std::string::npos);
}

// A typed envelope whose chainId field is not a parseable integer (here: an RLP list) must
// be rejected by the gate — typed txs always carry chainId in field 0; nullopt there is
// malformed, not a pre-EIP-155 exemption (m_prepare's typed-nullopt hard reject).
BOOST_AUTO_TEST_CASE(TypedEnvelopeUnparseableChainIdRejected)
{
    FakeTx tx;
    // 0x02 || rlp([<empty list as chainId>, nonce, ...]) — field 0 is a list, not an integer.
    bcos::bytes payload;
    auto append = [&payload](
                      bcos::bytes const& b) { payload.insert(payload.end(), b.begin(), b.end()); };
    bcos::bytes c0{0xc0};
    append(c0);
    append(c0);  // nonce placeholder (unused — the gate fails at chainId)
    bcos::bytes out{static_cast<bcos::byte>(0x02)};
    rlp::encodeHeader(out, {.isList = true, .payloadLength = payload.size()});
    out.insert(out.end(), payload.begin(), payload.end());
    tx.m_extraBytes = out;
    auto const gate = envelopeChainIdMismatch(tx, 10);
    BOOST_REQUIRE(gate.has_value());
    BOOST_CHECK(std::string(*gate).find("missing a parseable chainId") != std::string::npos);
}

// Consistent mirror + envelope passes the cross-check.
BOOST_AUTO_TEST_CASE(ConsistentMirrorPasses)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(
        10, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {0xde, 0xad});
    tx.m_value = bcos::u256{5};
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_input = {0xde, 0xad};
    BOOST_CHECK(!envelopeExecutionFieldsMismatch(tx, evmTxOf(tx)).has_value());
}

BOOST_AUTO_TEST_CASE(NonEmptyUnboundAccessListIsFailClosed)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(
        10, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {0xde, 0xad});
    tx.m_value = bcos::u256{5};
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_input = {0xde, 0xad};
    tx.m_accessList.push_back(bcos::protocol::Web3AccessListEntry{
        .account = bcos::Address(std::string(40, '1')), .storageKeys = {}});
    auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK(mismatch->find("accessList") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(NonEmptyUnboundBlobHashesAreFailClosed)
{
    FakeTx tx;
    tx.m_kind = 3;
    tx.m_extraBytes = blobOrAuthEnvelope(
        0x03, 10, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {0xde});
    tx.m_value = bcos::u256{5};
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_input = {0xde};
    tx.m_blobHashes.push_back(bcos::h256(1));
    auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK(mismatch->find("blobVersionedHashes") != std::string::npos);
}

// The field-index table's legacy branch (typed == false → [0,2,3,4,5]) is exercised: a bare
// legacy list envelope + matching mirror passes, a forged mirror value is rejected.
BOOST_AUTO_TEST_CASE(LegacyEnvelopeConsistentMirrorPasses)
{
    FakeTx tx;
    tx.m_kind = 0;
    tx.m_extraBytes = legacyEnvelope(
        7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {0xde, 0xad});
    tx.m_value = bcos::u256{5};
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_input = {0xde, 0xad};
    BOOST_CHECK(!envelopeExecutionFieldsMismatch(tx, evmTxOf(tx)).has_value());
}

BOOST_AUTO_TEST_CASE(LegacyEnvelopeValueDivergenceRejected)
{
    FakeTx tx;
    tx.m_kind = 0;
    tx.m_extraBytes =
        legacyEnvelope(7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {});
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_value = bcos::u256{999};  // forged
    auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK(std::string(*mismatch).find("value mismatch") != std::string::npos);
}

// The field-index table's 0x01 (2930) branch (envelopeKind == 0x01 → [1,3,4,5,6]) is exercised.
BOOST_AUTO_TEST_CASE(AccessListEnvelopeConsistentMirrorPasses)
{
    FakeTx tx;
    tx.m_kind = 1;
    tx.m_extraBytes = accessListEnvelope(
        10, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {0xde, 0xad});
    tx.m_value = bcos::u256{5};
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_input = {0xde, 0xad};
    BOOST_CHECK(!envelopeExecutionFieldsMismatch(tx, evmTxOf(tx)).has_value());
}

BOOST_AUTO_TEST_CASE(AccessListEnvelopeValueDivergenceRejected)
{
    FakeTx tx;
    tx.m_kind = 1;
    tx.m_extraBytes = accessListEnvelope(
        10, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {});
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_value = bcos::u256{999};  // forged
    auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK(std::string(*mismatch).find("value mismatch") != std::string::npos);
}

// Contract-creation shape: mirror `to` empty (→ evmTx.to = nullopt) and the envelope's `to`
// item empty — consistent, passes. An envelope carrying a 20-byte `to` then diverges.
BOOST_AUTO_TEST_CASE(ContractCreationEnvelopePasses)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(10, 7, 5000000, "0x", bcos::u256{5}, {0x60, 0x00});
    tx.m_value = bcos::u256{5};
    tx.m_to = "";  // creation
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_input = {0x60, 0x00};
    BOOST_CHECK(!envelopeExecutionFieldsMismatch(tx, evmTxOf(tx)).has_value());
}

BOOST_AUTO_TEST_CASE(ContractCreationEnvelopeToDivergenceRejected)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(
        10, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {});
    tx.m_value = bcos::u256{5};
    tx.m_to = "";  // creation, but the envelope carries a recipient
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK(std::string(*mismatch).find("to mismatch") != std::string::npos);
}

// Ethereum transaction `to` / `data` / nonce / gasLimit / value are byte strings or scalars,
// never RLP lists. Empty-list or 1-byte-list payloads must not pass the mirror gate.
BOOST_AUTO_TEST_CASE(ListShapedToAndDataAreRejected)
{
    FakeTx tx;
    tx.m_extraBytes = eip1559Envelope(10, 7, 5000000, "0x", bcos::u256{5}, {}, /*toIsList=*/true);
    tx.m_value = bcos::u256{5};
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    auto mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK_EQUAL(*mismatch, "to field is an RLP list");

    tx.m_extraBytes = eip1559Envelope(
        10, 7, 5000000, "0x", bcos::u256{5}, {}, /*toIsList=*/false, /*dataIsList=*/true);
    mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK_EQUAL(*mismatch, "data field is an RLP list");
}

BOOST_AUTO_TEST_CASE(ListShapedIntegerFieldsAreRejected)
{
    auto run1559 = [](bool nonceIsList, bool gasIsList, bool valueIsList, char const* needle) {
        FakeTx tx;
        tx.m_extraBytes = eip1559Envelope(10, 7, 5000000, "0x", bcos::u256{5}, {}, false, false,
            nonceIsList, gasIsList, valueIsList);
        tx.m_value = bcos::u256{5};
        tx.m_nonce = "0x7";
        tx.m_gasLimit = 5000000;
        auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
        BOOST_REQUIRE(mismatch.has_value());
        BOOST_CHECK_EQUAL(*mismatch, needle);
    };
    run1559(true, false, false, "nonce field is an RLP list");
    run1559(false, true, false, "gasLimit field is an RLP list");
    run1559(false, false, true, "value field is an RLP list");

    auto runLegacy = [](bool nonceIsList, bool gasIsList, bool valueIsList, char const* needle) {
        FakeTx tx;
        tx.m_kind = 0;
        tx.m_extraBytes = legacyEnvelope(
            7, 5000000, "0x", bcos::u256{5}, {}, nonceIsList, gasIsList, valueIsList);
        tx.m_value = bcos::u256{5};
        tx.m_nonce = "0x7";
        tx.m_gasLimit = 5000000;
        auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
        BOOST_REQUIRE(mismatch.has_value());
        BOOST_CHECK_EQUAL(*mismatch, needle);
    };
    runLegacy(true, false, false, "nonce field is an RLP list");
    runLegacy(false, true, false, "gasLimit field is an RLP list");
    runLegacy(false, false, true, "value field is an RLP list");

    auto runAccessList = [](bool nonceIsList, bool gasIsList, bool valueIsList,
                             char const* needle) {
        FakeTx tx;
        tx.m_kind = 1;
        tx.m_extraBytes = accessListEnvelope(
            10, 7, 5000000, "0x", bcos::u256{5}, {}, nonceIsList, gasIsList, valueIsList);
        tx.m_value = bcos::u256{5};
        tx.m_nonce = "0x7";
        tx.m_gasLimit = 5000000;
        auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
        BOOST_REQUIRE(mismatch.has_value());
        BOOST_CHECK_EQUAL(*mismatch, needle);
    };
    runAccessList(true, false, false, "nonce field is an RLP list");
    runAccessList(false, true, false, "gasLimit field is an RLP list");
    runAccessList(false, false, true, "value field is an RLP list");
}

BOOST_AUTO_TEST_CASE(BlockPathRejectsEmptySender)
{
    FakeTx tx;
    tx.m_sender.clear();
    BOOST_REQUIRE(blockPathZeroSender(evmTxOf(tx).sender).has_value());
    BOOST_CHECK_EQUAL(*blockPathZeroSender(evmTxOf(tx).sender), "empty sender");
    tx.m_sender.assign(sizeof(evmc_address), '\xaa');
    BOOST_CHECK(!blockPathZeroSender(evmTxOf(tx).sender).has_value());
    tx.m_sender.assign(sizeof(evmc_address), '\0');
    BOOST_REQUIRE(blockPathZeroSender(evmTxOf(tx).sender).has_value());
}

BOOST_AUTO_TEST_CASE(LegacyFixtureIsAFullUnprotectedSignedEnvelope)
{
    auto const envelope =
        legacyEnvelope(7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {});
    BOOST_CHECK(!bcos::rlp::protocol::web3ChainIdFromEnvelope(bcos::ref(envelope)).has_value());
}

// The 0x03 (blob) and 0x04 (7702) shapes share the typed (envelopeKind != 0x01) index layout
// [1,4,5,6,7] with 0x02 — a consistent mirror must pass (wrong indices would make it fail),
// and a forged mirror value must be rejected through the shared value index 6.
BOOST_AUTO_TEST_CASE(BlobEnvelopeConsistentMirrorPasses)
{
    FakeTx tx;
    tx.m_kind = 3;
    tx.m_extraBytes = blobOrAuthEnvelope(
        0x03, 10, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {0xde});
    tx.m_value = bcos::u256{5};
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_input = {0xde};
    BOOST_CHECK(!envelopeExecutionFieldsMismatch(tx, evmTxOf(tx)).has_value());
}

BOOST_AUTO_TEST_CASE(SetCodeEnvelopeConsistentMirrorPasses)
{
    FakeTx tx;
    tx.m_kind = 4;
    tx.m_extraBytes = blobOrAuthEnvelope(
        0x04, 10, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {0xde});
    tx.m_value = bcos::u256{5};
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_input = {0xde};
    BOOST_CHECK(!envelopeExecutionFieldsMismatch(tx, evmTxOf(tx)).has_value());
}

BOOST_AUTO_TEST_CASE(BlobEnvelopeValueDivergenceRejected)
{
    FakeTx tx;
    tx.m_kind = 3;
    tx.m_extraBytes = blobOrAuthEnvelope(
        0x03, 10, 7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {});
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_value = bcos::u256{999};  // forged
    auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK(std::string(*mismatch).find("value mismatch") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(BlockPathRejectsUnboundAuthorizationList)
{
    evmone::state::Transaction tx;
    BOOST_CHECK(!blockPathUnboundAuthorizationList(tx).has_value());
    tx.authorization_list.emplace_back();
    auto const gate = blockPathUnboundAuthorizationList(tx);
    BOOST_REQUIRE(gate.has_value());
    BOOST_CHECK_EQUAL(*gate, "authorizationList is not bound to the signed envelope");
}

// a stripped non-empty authorizationList on 0x04 is caught by
// envelopeExecutionFieldsMismatch (bindEnvelopeAuthorizationList). An empty mirror on 0x04 is
// deferred to opValidate (EMPTY_AUTHORIZATION_LIST) — this gate only rejects a non-empty list
// on non-0x04 txs whose signers are not envelope-bound yet.
BOOST_AUTO_TEST_CASE(BlockPathSetCodeEmptyMirrorListDeferredToOpValidate)
{
    evmone::state::Transaction setCodeTx;
    setCodeTx.type = evmone::state::Transaction::Type::set_code;
    BOOST_CHECK(setCodeTx.authorization_list.empty());
    BOOST_CHECK(!blockPathUnboundAuthorizationList(setCodeTx).has_value());
}

// The full accessList bind: a mirror STRIPPED against a non-empty envelope list must be
// rejected (previously only the mirror side was read, so this exact strip passed the gate
// and executed the signed envelope without its warm slots).
BOOST_AUTO_TEST_CASE(EnvelopeAccessListStrippedMirrorRejected)
{
    FakeTx tx;
    bcos::protocol::Web3AccessList envelopeList;
    envelopeList.push_back(bcos::protocol::Web3AccessListEntry{
        .account = bcos::Address(std::string(40, '1')), .storageKeys = {bcos::h256(1)}});
    tx.m_extraBytes = eip1559EnvelopeWithAccessList(10, 7, 5000000,
        "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {0xde, 0xad}, envelopeList);
    tx.m_value = bcos::u256{5};
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_input = {0xde, 0xad};
    // m_accessList stays empty: the mirror lost the envelope's entry.
    auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK_EQUAL(*mismatch, "accessList is not bound to the signed envelope");
}

// A mirror that faithfully echoes the envelope's accessList (addresses + storage keys)
// passes the gate — the honest caller is not punished for carrying a real list.
BOOST_AUTO_TEST_CASE(EnvelopeAccessListFullBindPasses)
{
    FakeTx tx;
    bcos::protocol::Web3AccessList envelopeList;
    envelopeList.push_back(
        bcos::protocol::Web3AccessListEntry{.account = bcos::Address(std::string(40, '1')),
            .storageKeys = {bcos::h256(1), bcos::h256(2)}});
    envelopeList.push_back(bcos::protocol::Web3AccessListEntry{
        .account = bcos::Address(std::string(40, '2')), .storageKeys = {}});
    tx.m_extraBytes = eip1559EnvelopeWithAccessList(10, 7, 5000000,
        "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {0xde, 0xad}, envelopeList);
    tx.m_value = bcos::u256{5};
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_input = {0xde, 0xad};
    tx.m_accessList = envelopeList;
    BOOST_CHECK(!envelopeExecutionFieldsMismatch(tx, evmTxOf(tx)).has_value());
}

// Both sides non-empty but divergent (different storage key): content-level mismatch gets
// its own message so the strip (presence) and the divergence (content) are distinguishable.
BOOST_AUTO_TEST_CASE(EnvelopeAccessListContentMismatchRejected)
{
    FakeTx tx;
    bcos::protocol::Web3AccessList envelopeList;
    envelopeList.push_back(bcos::protocol::Web3AccessListEntry{
        .account = bcos::Address(std::string(40, '1')), .storageKeys = {bcos::h256(1)}});
    tx.m_extraBytes = eip1559EnvelopeWithAccessList(10, 7, 5000000,
        "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {0xde, 0xad}, envelopeList);
    tx.m_value = bcos::u256{5};
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_input = {0xde, 0xad};
    tx.m_accessList.push_back(bcos::protocol::Web3AccessListEntry{
        .account = bcos::Address(std::string(40, '1')), .storageKeys = {bcos::h256(2)}});
    auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK_EQUAL(*mismatch, "accessList mismatch");
}

// The 0x03 blob bind: a matching mirror passes, a stripped mirror is rejected.
BOOST_AUTO_TEST_CASE(EnvelopeBlobHashesBind)
{
    FakeTx tx;
    tx.m_kind = 3;
    std::vector<bcos::h256> const hashes = {bcos::h256(1), bcos::h256(2)};
    tx.m_extraBytes = blobEnvelopeWithHashes(10, 7, 5000000,
        "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {0xde},
        /*accessList=*/{}, hashes);
    tx.m_value = bcos::u256{5};
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_input = {0xde};
    tx.m_blobHashes = hashes;
    BOOST_CHECK(!envelopeExecutionFieldsMismatch(tx, evmTxOf(tx)).has_value());

    // Stripped mirror: the envelope's hashes are gone from the mirror.
    tx.m_blobHashes.clear();
    auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK_EQUAL(*mismatch, "blobVersionedHashes is not bound to the signed envelope");
}

// The legacy posture is preserved: an envelope without an accessList field (legacy, and a
// fortiori 0x7e deposits) still rejects any non-empty mirror list.
BOOST_AUTO_TEST_CASE(LegacyEnvelopeMirrorAccessListRejected)
{
    FakeTx tx;
    tx.m_kind = 0;
    tx.m_extraBytes = legacyEnvelope(
        7, 5000000, "0x811a752c8cd697e3cb27279c330ed1ada745a8d7", bcos::u256{5}, {0xde, 0xad});
    tx.m_value = bcos::u256{5};
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_input = {0xde, 0xad};
    tx.m_accessList.push_back(bcos::protocol::Web3AccessListEntry{
        .account = bcos::Address(std::string(40, '1')), .storageKeys = {}});
    auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK_EQUAL(*mismatch, "accessList is not bound to the signed envelope");
}

// A type-0x03 envelope that stops after maxFeePerBlobGas (10 items, no blobVersionedHashes
// at idx 10) is malformed. The fewer-fields guard must reject it BEFORE the blob bind
// dereferences blobPayload: blobFieldsPresent is true for every 0x03 envelope, but
// blobPayload is engaged only when the walk reached idx 10 (regression: the old guard
// covered accessListPayload only, so a 10-item 0x03 hit an empty-optional dereference).
BOOST_AUTO_TEST_CASE(ShortType03EnvelopeFewerFieldsRejected)
{
    auto intItem = [](uint64_t v) {
        bcos::bytes out;
        rlp::encode(out, v);
        return out;
    };
    auto item = [](bcos::bytes const& payload) {
        bcos::bytes out;
        rlp::encode(out, bcos::bytesConstRef{payload.data(), payload.size()});
        return out;
    };
    bcos::bytes payload;
    auto append = [&payload](
                      bcos::bytes const& b) { payload.insert(payload.end(), b.begin(), b.end()); };
    append(intItem(10));           // chainId
    append(intItem(7));            // nonce
    append(intItem(30000000000));  // maxPriorityFeePerGas
    append(intItem(30000000000));  // maxFeePerGas
    append(intItem(5000000));      // gasLimit
    append(item(bcos::fromHex("811a752c8cd697e3cb27279c330ed1ada745a8d7")));  // to
    append(intItem(5));                                                       // value
    append(item({0xde}));                                                     // data
    payload.push_back(0xc0);                                                  // accessList (idx 8)
    append(intItem(1));  // maxFeePerBlobGas (idx 9) — no idx 10 follows

    bcos::bytes envelope{static_cast<bcos::byte>(0x03)};
    rlp::encodeHeader(envelope, {.isList = true, .payloadLength = payload.size()});
    envelope.insert(envelope.end(), payload.begin(), payload.end());

    FakeTx tx;
    tx.m_kind = 3;
    tx.m_extraBytes = envelope;
    tx.m_blobHashes = {bcos::h256(1)};  // non-empty mirror: the old code dereferenced the
                                        // disengaged blobPayload right here
    auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK_EQUAL(*mismatch, "envelope has fewer fields than the type requires");
}

// The bind's fixed-width memcpys must fit the remaining buffer: an accessList address item
// that declares 20 bytes but carries fewer is "malformed", never an out-of-bounds read
// (decodeHeader does not bound payloadLength against the view; the walk re-checks, these
// sub-decodes must too).
BOOST_AUTO_TEST_CASE(TruncatedAccessListAddressRejected)
{
    auto item = [](bcos::bytes const& payload) {
        bcos::bytes out;
        rlp::encode(out, bcos::bytesConstRef{payload.data(), payload.size()});
        return out;
    };
    auto intItem = [](uint64_t v) {
        bcos::bytes out;
        rlp::encode(out, v);
        return out;
    };
    // Address item: string header declaring 20, only 5 bytes present.
    bcos::bytes addrItem{0x94, 0x01, 0x02, 0x03, 0x04, 0x05};
    bcos::bytes entryPayload(addrItem);
    entryPayload.push_back(0xc0);  // empty storageKeys
    bcos::bytes entry;
    rlp::encodeHeader(entry, {.isList = true, .payloadLength = entryPayload.size()});
    entry.insert(entry.end(), entryPayload.begin(), entryPayload.end());
    bcos::bytes listPayload(entry);
    bcos::bytes accessListItem;
    rlp::encodeHeader(accessListItem, {.isList = true, .payloadLength = listPayload.size()});
    accessListItem.insert(accessListItem.end(), listPayload.begin(), listPayload.end());

    bcos::bytes payload;
    auto append = [&payload](
                      bcos::bytes const& b) { payload.insert(payload.end(), b.begin(), b.end()); };
    append(intItem(10));                                                      // chainId
    append(intItem(7));                                                       // nonce
    append(intItem(1000000000));                                              // gasPrice
    append(intItem(5000000));                                                 // gasLimit
    append(item(bcos::fromHex("811a752c8cd697e3cb27279c330ed1ada745a8d7")));  // to
    append(intItem(5));                                                       // value
    append(item({0xde}));                                                     // data
    append(accessListItem);  // accessList (idx 7 for 0x01)
    bcos::bytes envelope{static_cast<bcos::byte>(0x01)};
    rlp::encodeHeader(envelope, {.isList = true, .payloadLength = payload.size()});
    envelope.insert(envelope.end(), payload.begin(), payload.end());

    FakeTx tx;
    tx.m_kind = 1;
    tx.m_extraBytes = envelope;
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_value = bcos::u256{5};
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_input = {0xde};
    tx.m_accessList.push_back(bcos::protocol::Web3AccessListEntry{
        .account = bcos::Address(std::string(40, '1')), .storageKeys = {}});
    auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK_EQUAL(*mismatch, "accessList address is malformed");
}

// Same bound check for the blob field: a 32-declaring hash item with fewer bytes present is
// "malformed", not an out-of-bounds read of the truncated tail.
BOOST_AUTO_TEST_CASE(TruncatedBlobHashRejected)
{
    auto item = [](bcos::bytes const& payload) {
        bcos::bytes out;
        rlp::encode(out, bcos::bytesConstRef{payload.data(), payload.size()});
        return out;
    };
    auto intItem = [](uint64_t v) {
        bcos::bytes out;
        rlp::encode(out, v);
        return out;
    };
    // Blob hash item: string header declaring 32, only 5 bytes present.
    bcos::bytes hashItem{0xa0, 0x01, 0x02, 0x03, 0x04, 0x05};
    bcos::bytes blobListItem;
    rlp::encodeHeader(blobListItem, {.isList = true, .payloadLength = hashItem.size()});
    blobListItem.insert(blobListItem.end(), hashItem.begin(), hashItem.end());

    bcos::bytes payload;
    auto append = [&payload](
                      bcos::bytes const& b) { payload.insert(payload.end(), b.begin(), b.end()); };
    append(intItem(10));           // chainId
    append(intItem(7));            // nonce
    append(intItem(30000000000));  // maxPriorityFeePerGas
    append(intItem(30000000000));  // maxFeePerGas
    append(intItem(5000000));      // gasLimit
    append(item(bcos::fromHex("811a752c8cd697e3cb27279c330ed1ada745a8d7")));  // to
    append(intItem(5));                                                       // value
    append(item({0xde}));                                                     // data
    payload.push_back(0xc0);                                                  // accessList
    append(intItem(1));                                                       // maxFeePerBlobGas
    append(blobListItem);  // blobVersionedHashes (idx 10)
    bcos::bytes envelope{static_cast<bcos::byte>(0x03)};
    rlp::encodeHeader(envelope, {.isList = true, .payloadLength = payload.size()});
    envelope.insert(envelope.end(), payload.begin(), payload.end());

    FakeTx tx;
    tx.m_kind = 3;
    tx.m_extraBytes = envelope;
    tx.m_to = "0x811a752c8cd697e3cb27279c330ed1ada745a8d7";
    tx.m_value = bcos::u256{5};
    tx.m_nonce = "0x7";
    tx.m_gasLimit = 5000000;
    tx.m_input = {0xde};
    tx.m_blobHashes = {bcos::h256(1)};
    auto const mismatch = envelopeExecutionFieldsMismatch(tx, evmTxOf(tx));
    BOOST_REQUIRE(mismatch.has_value());
    BOOST_CHECK_EQUAL(*mismatch, "blobVersionedHashes entry is malformed");
}

BOOST_AUTO_TEST_SUITE_END()
