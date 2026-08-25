// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0
#pragma once

// Canonical RLP list encoder on bcos-codec. encodeTuple matches evmone::rlp::encode_tuple.

#include <bcos-codec/rlp/RLPEncode.h>
#include <bcos-evm/eth/state/transaction.hpp>
#include <concepts>
#include <cstdint>
#include <evmc/evmc.hpp>
#include <intx/intx.hpp>
#include <utility>
#include <vector>

namespace bcos::evm::eth::detail
{
/// RLP integer scalar for intx::uint256 (bcos-codec has no intx overload): 0 -> 0x80, otherwise
/// minimal big-endian + length prefix — identical to evmone::rlp::encode(const intx::uint256&).
inline void encodeRlp(bcos::bytes& to, const intx::uint256& x)
{
    if (x == 0)
    {
        to.push_back(bcos::codec::rlp::BYTES_HEAD_BASE);
        return;
    }
    uint8_t be[sizeof(intx::uint256)]{};
    intx::be::store(be, x);
    size_t first = 0;
    while (be[first] == 0)
        ++first;
    const size_t len = sizeof(intx::uint256) - first;
    if (len == 1 && be[first] < bcos::codec::rlp::BYTES_HEAD_BASE)
    {
        to.push_back(be[first]);
        return;
    }
    bcos::codec::rlp::encodeHeader(to, {.isList = false, .payloadLength = len});
    to.insert(to.end(), be + first, be + sizeof(intx::uint256));
}

/// Unsigned integer scalars (bcos-codec's UnsignedByte path already matches evmone for these).
/// Promoted to uint64_t first: the uint8_t path would instantiate the byte overload of
/// bcos::toCompactBigEndian, which is declared inline in the header but defined in a .cpp —
/// -Werror,-Wundefined-inline fires in any TU that instantiates it. uint64_t is byte-identical
/// RLP for every scalar the consumers produce (evmone promotes uint8_t to uint64_t the same way).
inline void encodeRlp(bcos::bytes& to, std::unsigned_integral auto v) noexcept
{
    bcos::codec::rlp::encode(to, static_cast<uint64_t>(v));
}

inline void encodeRlp(bcos::bytes& to, evmc::bytes_view v)
{
    bcos::codec::rlp::encode(to, bcos::bytesConstRef(v.data(), v.size()));
}

inline void encodeRlp(bcos::bytes& to, const evmc::bytes& b)
{
    encodeRlp(to, evmc::bytes_view(b.data(), b.size()));
}

inline void encodeRlp(bcos::bytes& to, const evmc::address& a)
{
    encodeRlp(to, evmc::bytes_view(a));
}

inline void encodeRlp(bcos::bytes& to, const evmc::bytes32& h)
{
    encodeRlp(to, evmc::bytes_view(h));
}

inline void encodeRlp(bcos::bytes& to, const std::vector<evmc::bytes32>& keys)
{
    bcos::bytes payload;
    for (const auto& key : keys)
        encodeRlp(payload, key);
    bcos::codec::rlp::encodeHeader(to, {.isList = true, .payloadLength = payload.size()});
    to.insert(to.end(), payload.begin(), payload.end());
}

/// Access list: rlp([ [address, [storageKey...]], ... ]) — each pair encodes as an RLP list of
/// (address, keys-list), matching evmone's pair overload.
inline void encodeRlp(bcos::bytes& to, const evmone::state::AccessList& al)
{
    bcos::bytes payload;
    for (const auto& [addr, keys] : al)
    {
        bcos::bytes item;
        encodeRlp(item, addr);
        encodeRlp(item, keys);
        bcos::codec::rlp::encodeHeader(payload, {.isList = true, .payloadLength = item.size()});
        payload.insert(payload.end(), item.begin(), item.end());
    }
    bcos::codec::rlp::encodeHeader(to, {.isList = true, .payloadLength = payload.size()});
    to.insert(to.end(), payload.begin(), payload.end());
}

/// EIP-7702 authorization list: rlp([ [chainId, address, nonce, v, r, s], ... ]) — same field
/// order as evmone::state::rlp_encode(const Authorization&).
inline void encodeRlp(bcos::bytes& to, const evmone::state::AuthorizationList& al)
{
    bcos::bytes payload;
    for (const auto& auth : al)
    {
        bcos::bytes item;
        encodeRlp(item, auth.chain_id);
        encodeRlp(item, auth.addr);
        encodeRlp(item, auth.nonce);
        encodeRlp(item, auth.v);
        encodeRlp(item, auth.r);
        encodeRlp(item, auth.s);
        bcos::codec::rlp::encodeHeader(payload, {.isList = true, .payloadLength = item.size()});
        payload.insert(payload.end(), item.begin(), item.end());
    }
    bcos::codec::rlp::encodeHeader(to, {.isList = true, .payloadLength = payload.size()});
    to.insert(to.end(), payload.begin(), payload.end());
}

/// evmone::rlp::encode_tuple equivalent: RLP-list-encodes the heterogeneous args. Returns
/// evmc::bytes so call sites keep `evmc::bytes{0x02} + encodeTuple(...)` concatenation intact.
template <typename... Args>
inline evmc::bytes encodeTuple(const Args&... args)
{
    bcos::bytes payload;
    (encodeRlp(payload, args), ...);
    bcos::bytes out;
    bcos::codec::rlp::encodeHeader(out, {.isList = true, .payloadLength = payload.size()});
    out.insert(out.end(), payload.begin(), payload.end());
    return evmc::bytes(out.begin(), out.end());
}
}  // namespace bcos::evm::eth::detail
