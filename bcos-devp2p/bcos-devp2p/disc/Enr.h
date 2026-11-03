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
 * @file Enr.h
 * @brief Ethereum Node Record (EIP-778 / devp2p enr.md): RLP codec, "v4" identity
 *        scheme sign/verify, `enr:` text form and typed accessors for the
 *        pre-defined keys plus the EIP-2124 `eth` fork-id entry.
 * @date 2026/11/3
 */
#pragma once

#include "../eth/ForkId.h"
#include <bcos-codec/rlp/Result.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/FixedBytes.h>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bcos::devp2p::disc
{
// 33-byte compressed form of a 64-byte uncompressed (x || y) secp256k1 public key.
bcos::bytes compressPublicKey(bytesConstRef _uncompressed64);
// Inverse of compressPublicKey; nullopt when the 33 bytes are not a curve point.
std::optional<bcos::bytes> decompressPublicKey(bytesConstRef _compressed33);
// Verify a 64-byte compact (r || s) secp256k1 signature over a 32-byte hash.
bool verifyCompactSignature(
    bytesConstRef _publicKey64, bytesConstRef _hash32, bytesConstRef _signature64);

// RFC 4648 §5 URL-safe base64 without padding (the ENR text alphabet).
std::string base64UrlEncode(bytesConstRef _data);
std::optional<bcos::bytes> base64UrlDecode(std::string_view _text);

// A node record. Key/value pairs are kept sorted by key with the value stored as its
// raw RLP item, so unknown keys round-trip byte-for-byte. The signature is only
// meaningful after sign() or a decode(); any mutation clears it.
class Enr
{
public:
    // enr.md: implementations should reject records larger than this.
    static constexpr size_t c_maxEncodedSize = 300;
    using Pair = std::pair<std::string, bcos::bytes>;  // key -> raw RLP-encoded value

    Enr() = default;

    // Structural decode of `[signature, seq, k, v, ...]`: size bound, sorted unique keys,
    // well-formed items. Does NOT verify the signature — call verify().
    static bcos::codec::rlp::RlpResult<Enr> decode(bytesConstRef _rlp);
    // `enr:<base64url>` text form, same checks as decode().
    static bcos::codec::rlp::RlpResult<Enr> fromText(std::string_view _text);

    // The canonical RLP record `[signature, seq, k, v, ...]`.
    bcos::bytes encode() const;
    // `enr:` + base64url(encode()).
    std::string toText() const;
    // The signed payload `[seq, k, v, ...]`.
    bcos::bytes content() const;

    // "v4" identity scheme: id == "v4", a 33-byte secp256k1 key and a valid 64-byte
    // signature over keccak256(content()).
    bool verify() const;
    // Set id="v4" and secp256k1=compress(pub(_privateKey)), then sign. Throws on a bad key.
    void sign(bytesConstRef _privateKey);
    bcos::bytes const& signature() const { return m_signature; }
    bool isSigned() const { return !m_signature.empty(); }

    uint64_t seq() const { return m_seq; }
    void setSeq(uint64_t _seq);

    std::vector<Pair> const& pairs() const { return m_pairs; }
    // Raw RLP item of `_key`, nullopt when absent.
    std::optional<bytesConstRef> rawValue(std::string_view _key) const;
    void setRaw(std::string _key, bcos::bytes _rlpValue);
    void erase(std::string_view _key);

    // Typed getters (nullopt when absent or malformed).
    std::optional<std::string> id() const;
    std::optional<bcos::bytes> secp256k1() const;  // 33 bytes, compressed
    std::optional<bcos::bytes> publicKey() const;  // 64 bytes, uncompressed
    // keccak256(uncompressed public key): the discovery node address.
    std::optional<bcos::h256> nodeId() const;
    std::optional<std::array<uint8_t, 4>> ip() const;
    std::optional<std::array<uint8_t, 16>> ip6() const;
    std::optional<uint16_t> tcp() const;
    std::optional<uint16_t> udp() const;
    std::optional<uint16_t> tcp6() const;
    std::optional<uint16_t> udp6() const;
    // EIP-2124 `eth` entry: `[[forkHash, forkNext]]` (geth forkid.enrEntry).
    std::optional<eth::ForkId> eth() const;

    // Typed setters (clear the signature).
    void setIp(std::array<uint8_t, 4> const& _ip);
    void setIp6(std::array<uint8_t, 16> const& _ip);
    void setTcp(uint16_t _port);
    void setUdp(uint16_t _port);
    void setEth(eth::ForkId const& _forkId);

    // Dotted IPv4 of ip(), empty when absent.
    std::string ipString() const;

private:
    std::optional<uint16_t> port(std::string_view _key) const;
    std::optional<bcos::bytes> bytesValue(std::string_view _key) const;
    void setBytes(std::string _key, bytesConstRef _value);
    void setUint(std::string _key, uint64_t _value);

    bcos::bytes m_signature;  // 64 bytes (r || s) once signed
    uint64_t m_seq{0};
    std::vector<Pair> m_pairs;  // sorted by key, unique
};
}  // namespace bcos::devp2p::disc
