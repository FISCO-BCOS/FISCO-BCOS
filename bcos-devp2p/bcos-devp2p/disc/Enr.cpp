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
 * @file Enr.cpp
 * @brief Ethereum Node Record codec and "v4" identity scheme.
 * @date 2026/11/3
 */
#include "Enr.h"
#include "../RlpTake.h"
#include "../Try.h"
#include "../rlpx/Crypto.h"
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-utilities/Base64.h>
#include <secp256k1.h>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace bcos::devp2p::disc
{
using bcos::codec::rlp::RlpResult;
using bcos::devp2p::detail::genericError;
using bcos::devp2p::detail::take;
using bcos::devp2p::detail::takeListPayload;
using bcos::devp2p::detail::takeRawItem;

// ---------------------------------------------------------------------------
// secp256k1 point helpers
// ---------------------------------------------------------------------------
bcos::bytes compressPublicKey(bytesConstRef _uncompressed64)
{
    if (_uncompressed64.size() != 64)
    {
        throw std::invalid_argument("compressPublicKey: expected a 64-byte public key");
    }
    std::array<unsigned char, 65> prefixed{};
    prefixed[0] = 0x04;
    std::memcpy(prefixed.data() + 1, _uncompressed64.data(), 64);
    secp256k1_pubkey point;
    if (secp256k1_ec_pubkey_parse(
            bcos::crypto::secp256k1Context(), &point, prefixed.data(), prefixed.size()) != 1)
    {
        throw std::invalid_argument("compressPublicKey: not a secp256k1 point");
    }
    bcos::bytes out(33);
    size_t outLen = out.size();
    secp256k1_ec_pubkey_serialize(
        bcos::crypto::secp256k1Context(), out.data(), &outLen, &point, SECP256K1_EC_COMPRESSED);
    return out;
}

std::optional<bcos::bytes> decompressPublicKey(bytesConstRef _compressed33)
{
    if (_compressed33.size() != 33)
    {
        return std::nullopt;
    }
    secp256k1_pubkey point;
    if (secp256k1_ec_pubkey_parse(bcos::crypto::secp256k1Context(), &point, _compressed33.data(),
            _compressed33.size()) != 1)
    {
        return std::nullopt;
    }
    std::array<unsigned char, 65> prefixed{};
    size_t outLen = prefixed.size();
    secp256k1_ec_pubkey_serialize(bcos::crypto::secp256k1Context(), prefixed.data(), &outLen,
        &point, SECP256K1_EC_UNCOMPRESSED);
    return bcos::bytes(prefixed.begin() + 1, prefixed.end());
}

bool verifyCompactSignature(
    bytesConstRef _publicKey64, bytesConstRef _hash32, bytesConstRef _signature64)
{
    if (_publicKey64.size() != 64 || _hash32.size() != 32 || _signature64.size() != 64)
    {
        return false;
    }
    std::array<unsigned char, 65> prefixed{};
    prefixed[0] = 0x04;
    std::memcpy(prefixed.data() + 1, _publicKey64.data(), 64);
    secp256k1_pubkey point;
    secp256k1_ecdsa_signature signature;
    auto* context = bcos::crypto::secp256k1Context();
    if (secp256k1_ec_pubkey_parse(context, &point, prefixed.data(), prefixed.size()) != 1 ||
        secp256k1_ecdsa_signature_parse_compact(context, &signature, _signature64.data()) != 1)
    {
        return false;
    }
    // Like geth's crypto.VerifySignature: a high-S signature is rejected (libsecp256k1
    // refuses non-normalized signatures in verify).
    return secp256k1_ecdsa_verify(context, &signature, _hash32.data(), &point) == 1;
}

// ---------------------------------------------------------------------------
// base64url (RFC 4648 §5, unpadded)
// ---------------------------------------------------------------------------
std::string base64UrlEncode(bytesConstRef _data)
{
    auto text = bcos::base64Encode(_data);
    for (auto& c : text)
    {
        if (c == '+')
        {
            c = '-';
        }
        else if (c == '/')
        {
            c = '_';
        }
    }
    while (!text.empty() && text.back() == '=')
    {
        text.pop_back();
    }
    return text;
}

std::optional<bcos::bytes> base64UrlDecode(std::string_view _text)
{
    std::string standard;
    standard.reserve(_text.size() + 3);
    for (char c : _text)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        {
            standard.push_back(c);
        }
        else if (c == '-')
        {
            standard.push_back('+');
        }
        else if (c == '_')
        {
            standard.push_back('/');
        }
        else
        {
            return std::nullopt;  // padding is not part of the unpadded alphabet either
        }
    }
    if (standard.size() % 4 == 1)
    {
        return std::nullopt;  // no byte string encodes to 4n+1 characters
    }
    if (standard.empty())
    {
        return bcos::bytes{};
    }
    while (standard.size() % 4 != 0)
    {
        standard.push_back('=');
    }
    return bcos::base64DecodeBytes(standard);
}

// ---------------------------------------------------------------------------
// RLP helpers
// ---------------------------------------------------------------------------
namespace
{
bcos::bytes encodeBytesItem(bytesConstRef _value)
{
    bcos::bytes out;
    bcos::codec::rlp::encode(out, _value);
    return out;
}

bcos::bytes encodeUintItem(uint64_t _value)
{
    bcos::bytes out;
    bcos::codec::rlp::encode(out, _value);
    return out;
}

template <typename T>
std::optional<T> decodeWhole(bytesConstRef _raw)
{
    bcos::bytesRef view(const_cast<bcos::byte*>(_raw.data()), _raw.size());
    auto value = take<T>(view);
    if (!value || !view.empty())
    {
        return std::nullopt;
    }
    return std::move(*value);
}
}  // namespace

// ---------------------------------------------------------------------------
// Enr
// ---------------------------------------------------------------------------
RlpResult<Enr> Enr::decode(bytesConstRef _rlp)
{
    if (_rlp.size() > c_maxEncodedSize)
    {
        return std::unexpected(genericError("enr: record exceeds 300 bytes"));
    }
    bcos::bytesRef view(const_cast<bcos::byte*>(_rlp.data()), _rlp.size());
    RLP_TRY(auto items, takeListPayload(view, "enr: record must be an RLP list"));
    if (!view.empty())
    {
        return std::unexpected(genericError("enr: trailing bytes after the record"));
    }
    Enr record;
    RLP_TRY(record.m_signature, take<bcos::bytes>(items));
    RLP_TRY(record.m_seq, take<uint64_t>(items));
    std::string previousKey;
    bool first = true;
    while (!items.empty())
    {
        RLP_TRY(auto key, take<std::string>(items));
        if (items.empty())
        {
            return std::unexpected(genericError("enr: key without a value"));
        }
        RLP_TRY(auto value, takeRawItem(items));
        if (!first && previousKey.compare(key) >= 0)
        {
            return std::unexpected(genericError("enr: keys must be sorted and unique"));
        }
        first = false;
        previousKey = key;
        record.m_pairs.emplace_back(std::move(key), std::move(value));
    }
    return record;
}

RlpResult<Enr> Enr::fromText(std::string_view _text)
{
    constexpr std::string_view prefix = "enr:";
    if (_text.substr(0, prefix.size()) != prefix)
    {
        return std::unexpected(genericError("enr: text form must start with 'enr:'"));
    }
    auto raw = base64UrlDecode(_text.substr(prefix.size()));
    if (!raw)
    {
        return std::unexpected(genericError("enr: invalid base64url"));
    }
    return decode(bcos::ref(*raw));
}

bcos::bytes Enr::content() const
{
    bcos::bytes payload = encodeUintItem(m_seq);
    for (auto const& [key, value] : m_pairs)
    {
        auto keyItem = encodeBytesItem(
            bytesConstRef(reinterpret_cast<bcos::byte const*>(key.data()), key.size()));
        payload.insert(payload.end(), keyItem.begin(), keyItem.end());
        payload.insert(payload.end(), value.begin(), value.end());
    }
    bcos::bytes out;
    bcos::codec::rlp::encodeHeader(out, {.isList = true, .payloadLength = payload.size()});
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

bcos::bytes Enr::encode() const
{
    bcos::bytes payload = encodeBytesItem(bcos::ref(m_signature));
    auto seqItem = encodeUintItem(m_seq);
    payload.insert(payload.end(), seqItem.begin(), seqItem.end());
    for (auto const& [key, value] : m_pairs)
    {
        auto keyItem = encodeBytesItem(
            bytesConstRef(reinterpret_cast<bcos::byte const*>(key.data()), key.size()));
        payload.insert(payload.end(), keyItem.begin(), keyItem.end());
        payload.insert(payload.end(), value.begin(), value.end());
    }
    bcos::bytes out;
    bcos::codec::rlp::encodeHeader(out, {.isList = true, .payloadLength = payload.size()});
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::string Enr::toText() const
{
    return "enr:" + base64UrlEncode(bcos::ref(encode()));
}

bool Enr::verify() const
{
    auto scheme = id();
    if (!scheme || *scheme != "v4" || m_signature.size() != 64)
    {
        return false;
    }
    auto key = publicKey();
    if (!key)
    {
        return false;
    }
    auto hash = rlpx::keccak256(bcos::ref(content()));
    return verifyCompactSignature(bcos::ref(*key), bcos::ref(hash), bcos::ref(m_signature));
}

void Enr::sign(bytesConstRef _privateKey)
{
    rlpx::EccKeyPair keyPair(_privateKey.toBytes());
    setBytes("id", bytesConstRef(reinterpret_cast<bcos::byte const*>("v4"), 2));
    setBytes("secp256k1", bcos::ref(compressPublicKey(bcos::ref(keyPair.publicKey()))));
    auto hash = rlpx::keccak256(bcos::ref(content()));
    auto recoverable = rlpx::signRecoverable(bcos::ref(hash), _privateKey);
    m_signature.assign(recoverable.begin(), recoverable.begin() + 64);
    if (encode().size() > c_maxEncodedSize)
    {
        m_signature.clear();
        throw std::invalid_argument("Enr::sign: record exceeds 300 bytes");
    }
}

void Enr::setSeq(uint64_t _seq)
{
    m_seq = _seq;
    m_signature.clear();
}

std::optional<bytesConstRef> Enr::rawValue(std::string_view _key) const
{
    auto it = std::lower_bound(m_pairs.begin(), m_pairs.end(), _key,
        [](Pair const& _pair, std::string_view _k) { return _pair.first < _k; });
    if (it == m_pairs.end() || it->first != _key)
    {
        return std::nullopt;
    }
    return bcos::ref(it->second);
}

void Enr::setRaw(std::string _key, bcos::bytes _rlpValue)
{
    m_signature.clear();
    auto it = std::lower_bound(m_pairs.begin(), m_pairs.end(), _key,
        [](Pair const& _pair, std::string const& _k) { return _pair.first < _k; });
    if (it != m_pairs.end() && it->first == _key)
    {
        it->second = std::move(_rlpValue);
        return;
    }
    m_pairs.emplace(it, std::move(_key), std::move(_rlpValue));
}

void Enr::erase(std::string_view _key)
{
    auto it = std::find_if(
        m_pairs.begin(), m_pairs.end(), [&](Pair const& _pair) { return _pair.first == _key; });
    if (it != m_pairs.end())
    {
        m_pairs.erase(it);
        m_signature.clear();
    }
}

std::optional<bcos::bytes> Enr::bytesValue(std::string_view _key) const
{
    auto raw = rawValue(_key);
    if (!raw)
    {
        return std::nullopt;
    }
    return decodeWhole<bcos::bytes>(*raw);
}

std::optional<uint16_t> Enr::port(std::string_view _key) const
{
    auto raw = rawValue(_key);
    if (!raw)
    {
        return std::nullopt;
    }
    auto value = decodeWhole<uint64_t>(*raw);
    if (!value || *value > 0xffff)
    {
        return std::nullopt;
    }
    return static_cast<uint16_t>(*value);
}

std::optional<std::string> Enr::id() const
{
    auto value = bytesValue("id");
    if (!value)
    {
        return std::nullopt;
    }
    return std::string(value->begin(), value->end());
}

std::optional<bcos::bytes> Enr::secp256k1() const
{
    auto value = bytesValue("secp256k1");
    if (!value || value->size() != 33)
    {
        return std::nullopt;
    }
    return value;
}

std::optional<bcos::bytes> Enr::publicKey() const
{
    auto compressed = secp256k1();
    if (!compressed)
    {
        return std::nullopt;
    }
    return decompressPublicKey(bcos::ref(*compressed));
}

std::optional<bcos::h256> Enr::nodeId() const
{
    auto key = publicKey();
    if (!key)
    {
        return std::nullopt;
    }
    return bcos::h256(rlpx::keccak256(bcos::ref(*key)));
}

std::optional<std::array<uint8_t, 4>> Enr::ip() const
{
    auto value = bytesValue("ip");
    if (!value || value->size() != 4)
    {
        return std::nullopt;
    }
    std::array<uint8_t, 4> out{};
    std::copy(value->begin(), value->end(), out.begin());
    return out;
}

std::optional<std::array<uint8_t, 16>> Enr::ip6() const
{
    auto value = bytesValue("ip6");
    if (!value || value->size() != 16)
    {
        return std::nullopt;
    }
    std::array<uint8_t, 16> out{};
    std::copy(value->begin(), value->end(), out.begin());
    return out;
}

std::optional<uint16_t> Enr::tcp() const
{
    return port("tcp");
}
std::optional<uint16_t> Enr::udp() const
{
    return port("udp");
}
std::optional<uint16_t> Enr::tcp6() const
{
    return port("tcp6");
}
std::optional<uint16_t> Enr::udp6() const
{
    return port("udp6");
}

std::optional<eth::ForkId> Enr::eth() const
{
    auto raw = rawValue("eth");
    if (!raw)
    {
        return std::nullopt;
    }
    // [[forkHash(4 bytes), forkNext], ...]: geth's forkid.enrEntry with a tail we ignore.
    bcos::bytesRef view(const_cast<bcos::byte*>(raw->data()), raw->size());
    auto outer = takeListPayload(view, "eth: expected a list");
    if (!outer)
    {
        return std::nullopt;
    }
    auto inner = takeListPayload(*outer, "eth: expected a fork-id list");
    if (!inner)
    {
        return std::nullopt;
    }
    auto hashBytes = take<bcos::bytes>(*inner);
    if (!hashBytes || hashBytes->size() != 4)
    {
        return std::nullopt;
    }
    auto next = take<uint64_t>(*inner);
    if (!next)
    {
        return std::nullopt;
    }
    eth::ForkId forkId;
    forkId.hash = (static_cast<uint32_t>((*hashBytes)[0]) << 24) |
                  (static_cast<uint32_t>((*hashBytes)[1]) << 16) |
                  (static_cast<uint32_t>((*hashBytes)[2]) << 8) |
                  static_cast<uint32_t>((*hashBytes)[3]);
    forkId.next = *next;
    return forkId;
}

void Enr::setBytes(std::string _key, bytesConstRef _value)
{
    setRaw(std::move(_key), encodeBytesItem(_value));
}

void Enr::setUint(std::string _key, uint64_t _value)
{
    setRaw(std::move(_key), encodeUintItem(_value));
}

void Enr::setIp(std::array<uint8_t, 4> const& _ip)
{
    setBytes("ip", bytesConstRef(_ip.data(), _ip.size()));
}

void Enr::setIp6(std::array<uint8_t, 16> const& _ip)
{
    setBytes("ip6", bytesConstRef(_ip.data(), _ip.size()));
}

void Enr::setTcp(uint16_t _port)
{
    setUint("tcp", _port);
}

void Enr::setUdp(uint16_t _port)
{
    setUint("udp", _port);
}

void Enr::setEth(eth::ForkId const& _forkId)
{
    std::array<bcos::byte, 4> forkHash{static_cast<bcos::byte>((_forkId.hash >> 24) & 0xff),
        static_cast<bcos::byte>((_forkId.hash >> 16) & 0xff),
        static_cast<bcos::byte>((_forkId.hash >> 8) & 0xff),
        static_cast<bcos::byte>(_forkId.hash & 0xff)};
    bcos::bytes innerPayload = encodeBytesItem(bytesConstRef(forkHash.data(), forkHash.size()));
    auto nextItem = encodeUintItem(_forkId.next);
    innerPayload.insert(innerPayload.end(), nextItem.begin(), nextItem.end());
    bcos::bytes inner;
    bcos::codec::rlp::encodeHeader(inner, {.isList = true, .payloadLength = innerPayload.size()});
    inner.insert(inner.end(), innerPayload.begin(), innerPayload.end());
    bcos::bytes outer;
    bcos::codec::rlp::encodeHeader(outer, {.isList = true, .payloadLength = inner.size()});
    outer.insert(outer.end(), inner.begin(), inner.end());
    setRaw("eth", std::move(outer));
}

std::string Enr::ipString() const
{
    auto address = ip();
    if (!address)
    {
        return {};
    }
    return std::to_string((*address)[0]) + "." + std::to_string((*address)[1]) + "." +
           std::to_string((*address)[2]) + "." + std::to_string((*address)[3]);
}
}  // namespace bcos::devp2p::disc
