/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief evm precompiled
 * @file Precompiled.cpp
 * @author: xingqiangbai
 * @date: 2021-05-24
 */

#include "../vm/Precompiled.h"
#include "../Common.h"
#include "wedpr-crypto/WedprBn128.h"
#include "wedpr-crypto/WedprCrypto.h"
#include <bcos-utilities/Log.h>

using namespace std;
using namespace bcos;
using namespace bcos::crypto;

namespace bcos
{
namespace executor
{
PrecompiledRegistrar* PrecompiledRegistrar::s_this = nullptr;

PrecompiledExecutor const& PrecompiledRegistrar::executor(std::string const& _name)
{
    if (!get()->m_execs.count(_name))
        BOOST_THROW_EXCEPTION(ExecutorNotFound());
    return get()->m_execs[_name];
}

PrecompiledPricer const& PrecompiledRegistrar::pricer(std::string const& _name)
{
    if (!get()->m_pricers.count(_name))
        BOOST_THROW_EXCEPTION(PricerNotFound());
    return get()->m_pricers[_name];
}

}  // namespace executor
}  // namespace bcos

namespace
{
ETH_REGISTER_PRECOMPILED(ecrecover)(bytesConstRef _in)
{
    // When supported_version> = v2.4.0, ecRecover uniformly calls the ECDSA verification function
    return bcos::crypto::ecRecover(_in);
}

ETH_REGISTER_PRECOMPILED(sha256)(bytesConstRef _in)
{
    return {true, bcos::crypto::sha256(_in).asBytes()};
}

ETH_REGISTER_PRECOMPILED(ripemd160)(bytesConstRef _in)
{
    return {true, h256(bcos::crypto::ripemd160(_in), h256::AlignRight).asBytes()};
}

ETH_REGISTER_PRECOMPILED(identity)(bytesConstRef _in)
{
    return {true, _in.toBytes()};
}

// Parse _count bytes of _in starting with _begin offset as big endian int.
// If there's not enough bytes in _in, consider it infinitely right-padded with zeroes.
bigint parseBigEndianRightPadded(bytesConstRef _in, bigint const& _begin, bigint const& _count)
{
    if (_begin > _in.count())
        return 0;
    assert(_count <= numeric_limits<size_t>::max() / 8);  // Otherwise, the return value would not
                                                          // fit in the memory.

    size_t const begin{_begin};
    size_t const count{_count};

    // crop _in, not going beyond its size
    bytesConstRef cropped = _in.getCroppedData(begin, min(count, _in.count() - begin));

    bigint ret = fromBigEndian<bigint>(cropped);
    // shift as if we had right-padding zeroes
    assert(count - cropped.count() <= numeric_limits<size_t>::max() / 8);
    ret <<= 8 * (count - cropped.count());

    return ret;
}

ETH_REGISTER_PRECOMPILED(modexp)(bytesConstRef _in)
{
    // This is a protocol of bignumber modular
    // Described here:
    // https://github.com/ethereum/EIPs/blob/master/EIPS/eip-198.md
    bigint const baseLength(parseBigEndianRightPadded(_in, 0, 32));
    bigint const expLength(parseBigEndianRightPadded(_in, 32, 32));
    bigint const modLength(parseBigEndianRightPadded(_in, 64, 32));
    assert(modLength <= numeric_limits<size_t>::max() / 8);   // Otherwise gas should be too
                                                              // expensive.
    assert(baseLength <= numeric_limits<size_t>::max() / 8);  // Otherwise, gas should be too
                                                              // expensive.
    if (modLength == 0 && baseLength == 0)
        return {true, bytes{}};  // This is a special case where expLength can be very big.
    assert(expLength <= numeric_limits<size_t>::max() / 8);

    bigint const base(parseBigEndianRightPadded(_in, 96, baseLength));
    bigint const exp(parseBigEndianRightPadded(_in, 96 + baseLength, expLength));
    bigint const mod(parseBigEndianRightPadded(_in, 96 + baseLength + expLength, modLength));

    bigint const result = mod != 0 ? boost::multiprecision::powm(base, exp, mod) : bigint{0};

    size_t const retLength(modLength);
    bytes ret(retLength);
    toBigEndian(result, ret);

    return {true, ret};
}

namespace
{
bigint expLengthAdjust(bigint const& _expOffset, bigint const& _expLength, bytesConstRef _in)
{
    if (_expLength <= 32)
    {
        bigint const exp(parseBigEndianRightPadded(_in, _expOffset, _expLength));
        return exp ? msb(exp) : 0;
    }
    else
    {
        bigint const expFirstWord(parseBigEndianRightPadded(_in, _expOffset, 32));
        size_t const highestBit(expFirstWord ? msb(expFirstWord) : 0);
        return 8 * (_expLength - 32) + highestBit;
    }
}

bigint multComplexity(bigint const& _x)
{
    if (_x <= 64)
        return _x * _x;
    if (_x <= 1024)
        return (_x * _x) / 4 + 96 * _x - 3072;
    else
        return (_x * _x) / 16 + 480 * _x - 199680;
}
}  // namespace

ETH_REGISTER_PRECOMPILED_PRICER(modexp)(bytesConstRef _in)
{
    bigint const baseLength(parseBigEndianRightPadded(_in, 0, 32));
    bigint const expLength(parseBigEndianRightPadded(_in, 32, 32));
    bigint const modLength(parseBigEndianRightPadded(_in, 64, 32));

    bigint const maxLength(max(modLength, baseLength));
    bigint const adjustedExpLength(expLengthAdjust(baseLength + 96, expLength, _in));

    return multComplexity(maxLength) * max<bigint>(adjustedExpLength, 1) / 20;
}

ETH_REGISTER_PRECOMPILED(alt_bn128_G1_add)(bytesConstRef _in)
{
    pair<bool, bytes> ret{false, bytes(64, 0)};
    CInputBuffer in{(const char*)_in.data(), _in.size()};
    COutputBuffer result{(char*)ret.second.data(), 64};
    if (wedpr_fb_alt_bn128_g1_add(&in, &result) != 0)
    {
        return ret;
    }
    ret.first = true;
    return ret;
}

ETH_REGISTER_PRECOMPILED(alt_bn128_G1_mul)(bytesConstRef _in)
{
    pair<bool, bytes> ret{false, bytes(64, 0)};
    CInputBuffer in{(const char*)_in.data(), _in.size()};
    COutputBuffer result{(char*)ret.second.data(), 64};
    if (wedpr_fb_alt_bn128_g1_mul(&in, &result) != 0)
    {
        return ret;
    }
    ret.first = true;
    return ret;
}

ETH_REGISTER_PRECOMPILED(alt_bn128_pairing_product)(bytesConstRef _in)
{
    // Input: list of pairs of G1 and G2 points
    // Output: 1 if pairing evaluates to 1, 0 otherwise (left-padded to 32 bytes)
    pair<bool, bytes> ret{false, bytes(32, 0)};
    size_t constexpr pairSize = 2 * 32 + 2 * 64;
    size_t const pairs = _in.size() / pairSize;
    if (pairs * pairSize != _in.size())
    {
        // Invalid length.
        return ret;
    }

    CInputBuffer in{(const char*)_in.data(), _in.size()};
    COutputBuffer result{(char*)ret.second.data(), 32};
    if (wedpr_fb_alt_bn128_pairing_product(&in, &result) != 0)
    {
        return ret;
    }
    ret.first = true;
    return ret;
}

ETH_REGISTER_PRECOMPILED_PRICER(alt_bn128_pairing_product)
(bytesConstRef _in)
{
    auto const k = _in.size() / 192;
    return 45000 + k * 34000;
}

ETH_REGISTER_PRECOMPILED(blake2_compression)(bytesConstRef _in)
{
    static constexpr size_t roundsSize = 4;
    static constexpr size_t stateVectorSize = 8 * 8;
    static constexpr size_t messageBlockSize = 16 * 8;
    static constexpr size_t offsetCounterSize = 8;
    static constexpr size_t finalBlockIndicatorSize = 1;
    static constexpr size_t totalInputSize = roundsSize + stateVectorSize + messageBlockSize +
                                             2 * offsetCounterSize + finalBlockIndicatorSize;

    if (_in.size() != totalInputSize)
        return {false, {}};

    auto const rounds = fromBigEndian<uint32_t>(_in.getCroppedData(0, roundsSize));
    auto const stateVector = _in.getCroppedData(roundsSize, stateVectorSize);
    auto const messageBlockVector =
        _in.getCroppedData(roundsSize + stateVectorSize, messageBlockSize);
    auto const offsetCounter0 =
        _in.getCroppedData(roundsSize + stateVectorSize + messageBlockSize, offsetCounterSize);
    auto const offsetCounter1 = _in.getCroppedData(
        roundsSize + stateVectorSize + messageBlockSize + offsetCounterSize, offsetCounterSize);
    uint8_t const finalBlockIndicator =
        _in[roundsSize + stateVectorSize + messageBlockSize + 2 * offsetCounterSize];

    if (finalBlockIndicator != 0 && finalBlockIndicator != 1)
        return {false, {}};

    return {true, bcos::crypto::blake2FCompression(rounds, stateVector, offsetCounter0,
                      offsetCounter1, finalBlockIndicator, messageBlockVector)};
}

ETH_REGISTER_PRECOMPILED_PRICER(blake2_compression)
(bytesConstRef _in)
{
    auto const rounds = fromBigEndian<uint32_t>(_in.getCroppedData(0, 4));
    return rounds;
}


}  // namespace

namespace bcos
{
namespace precompiled
{
std::optional<storage::Table> Precompiled::createTable(
    storage::StateStorageInterface::Ptr _tableFactory, const std::string& tableName,
    const std::string& valueField)
{
    auto ret = _tableFactory->createTable(tableName, valueField);
    return ret ? _tableFactory->openTable(tableName) : nullopt;
}
}  // namespace precompiled


namespace crypto
{
// add sha2 -- sha256 to this file begin
h256 sha256(bytesConstRef _in) noexcept
{
    h256 ret;
    CInputBuffer in{(const char*)_in.data(), _in.size()};
    COutputBuffer result{(char*)ret.data(), h256::SIZE};
    if (wedpr_sha256_hash(&in, &result) != 0)
    {  // TODO: add some log
        return ret;
    }
    return ret;
}

h160 ripemd160(bytesConstRef _in)
{
    h160 ret;
    CInputBuffer in{(const char*)_in.data(), _in.size()};
    COutputBuffer result{(char*)ret.data(), h160::SIZE};
    if (wedpr_ripemd160_hash(&in, &result) != 0)
    {  // TODO: add some log
        return ret;
    }
    return ret;
}

namespace
{
// The Blake 2 F compression function implemenation is based on the reference implementation,
// see https://github.com/BLAKE2/BLAKE2/blob/master/ref/blake2b-ref.c
// The changes in original code were done mostly to accommodate variable round number and to remove
// unnecessary big endian support.
constexpr size_t BLAKE2B_BLOCKBYTES = 128;

struct blake2b_state
{
    uint64_t h[8];
    uint64_t t[2];
    uint64_t f[2];
    uint8_t buf[BLAKE2B_BLOCKBYTES];
    size_t buflen;
    size_t outlen;
    uint8_t last_node;
};

// clang-format off
constexpr uint64_t blake2b_IV[8] =
{
  0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
  0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
  0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
  0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
};

constexpr uint8_t blake2b_sigma[12][16] =
{
  {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 } ,
  { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 } ,
  { 11,  8, 12,  0,  5,  2, 15, 13, 10, 14,  3,  6,  7,  1,  9,  4 } ,
  {  7,  9,  3,  1, 13, 12, 11, 14,  2,  6,  5, 10,  4,  0, 15,  8 } ,
  {  9,  0,  5,  7,  2,  4, 10, 15, 14,  1, 11, 12,  6,  8,  3, 13 } ,
  {  2, 12,  6, 10,  0, 11,  8,  3,  4, 13,  7,  5, 15, 14,  1,  9 } ,
  { 12,  5,  1, 15, 14, 13,  4, 10,  0,  7,  6,  3,  9,  2,  8, 11 } ,
  { 13, 11,  7, 14, 12,  1,  3,  9,  5,  0, 15,  4,  8,  6,  2, 10 } ,
  {  6, 15, 14,  9, 11,  3,  0,  8, 12,  2, 13,  7,  1,  4, 10,  5 } ,
  { 10,  2,  8,  4,  7,  6,  1,  5, 15, 11,  9, 14,  3, 12, 13 , 0 } ,
};
// clang-format on

inline uint64_t load64(const void* src) noexcept
{
    uint64_t w;
    memcpy(&w, src, sizeof w);
    return w;
}

inline constexpr uint64_t rotr64(uint64_t w, unsigned c) noexcept
{
    return (w >> c) | (w << (64 - c));
}

inline void G(uint8_t r, uint8_t i, uint64_t& a, uint64_t& b, uint64_t& c, uint64_t& d,
    const uint64_t* m) noexcept
{
    a = a + b + m[blake2b_sigma[r][2 * i + 0]];
    d = rotr64(d ^ a, 32);
    c = c + d;
    b = rotr64(b ^ c, 24);
    a = a + b + m[blake2b_sigma[r][2 * i + 1]];
    d = rotr64(d ^ a, 16);
    c = c + d;
    b = rotr64(b ^ c, 63);
}

inline void ROUND(uint32_t round, uint64_t* v, const uint64_t* m) noexcept
{
    uint8_t const r = round % 10;
    G(r, 0, v[0], v[4], v[8], v[12], m);
    G(r, 1, v[1], v[5], v[9], v[13], m);
    G(r, 2, v[2], v[6], v[10], v[14], m);
    G(r, 3, v[3], v[7], v[11], v[15], m);
    G(r, 4, v[0], v[5], v[10], v[15], m);
    G(r, 5, v[1], v[6], v[11], v[12], m);
    G(r, 6, v[2], v[7], v[8], v[13], m);
    G(r, 7, v[3], v[4], v[9], v[14], m);
}


void blake2b_compress(
    uint32_t rounds, blake2b_state* S, const uint8_t block[BLAKE2B_BLOCKBYTES]) noexcept
{
    uint64_t m[16];
    uint64_t v[16];

    for (size_t i = 0; i < 16; ++i)
        m[i] = load64(block + i * sizeof(m[i]));

    for (size_t i = 0; i < 8; ++i)
        v[i] = S->h[i];

    v[8] = blake2b_IV[0];
    v[9] = blake2b_IV[1];
    v[10] = blake2b_IV[2];
    v[11] = blake2b_IV[3];
    v[12] = blake2b_IV[4] ^ S->t[0];
    v[13] = blake2b_IV[5] ^ S->t[1];
    v[14] = blake2b_IV[6] ^ S->f[0];
    v[15] = blake2b_IV[7] ^ S->f[1];

    for (uint32_t r = 0; r < rounds; ++r)
        ROUND(r, v, m);

    for (size_t i = 0; i < 8; ++i)
        S->h[i] = S->h[i] ^ v[i] ^ v[i + 8];
}

}  // namespace

bytes blake2FCompression(uint32_t _rounds, bytesConstRef _stateVector, bytesConstRef _t0,
    bytesConstRef _t1, bool _lastBlock, bytesConstRef _messageBlockVector)
{
    if (_stateVector.size() != sizeof(blake2b_state::h))
        BOOST_THROW_EXCEPTION(InvalidInputSize());

    blake2b_state s{};
    std::memcpy(&s.h, _stateVector.data(), _stateVector.size());

    if (_t0.size() != sizeof(s.t[0]) || _t1.size() != sizeof(s.t[1]))
        BOOST_THROW_EXCEPTION(InvalidInputSize());

    s.t[0] = load64(_t0.data());
    s.t[1] = load64(_t1.data());
    s.f[0] = _lastBlock ? std::numeric_limits<uint64_t>::max() : 0;

    if (_messageBlockVector.size() != BLAKE2B_BLOCKBYTES)
        BOOST_THROW_EXCEPTION(InvalidInputSize());

    uint8_t block[BLAKE2B_BLOCKBYTES];
    std::copy(_messageBlockVector.begin(), _messageBlockVector.end(), &block[0]);

    blake2b_compress(_rounds, &s, block);

    bytes result(sizeof(s.h));
    std::memcpy(&result[0], &s.h[0], result.size());

    return result;
}

const int RSV_LENGTH = 65;
const int PUBLIC_KEY_LENGTH = 64;
pair<bool, bytes> ecRecover(bytesConstRef _in)
{  // _in is hash(32),v(32),r(32),s(32), return address
    BCOS_LOG(TRACE) << LOG_BADGE("Precompiled") << LOG_DESC("ecRecover: ") << _in.size();
    byte rawRSV[RSV_LENGTH];
    memcpy(rawRSV, _in.data() + 64, RSV_LENGTH - 1);
    rawRSV[RSV_LENGTH - 1] = (byte)((int)_in[63] - 27);
    CInputBuffer msgHash{(const char*)_in.data(), crypto::HashType::SIZE};
    CInputBuffer rsv{(const char*)rawRSV, RSV_LENGTH};

    pair<bool, bytes> ret{true, bytes(crypto::HashType::SIZE, 0)};
    bytes publicKeyBytes(64, 0);
    COutputBuffer publicKey{(char*)publicKeyBytes.data(), PUBLIC_KEY_LENGTH};

    BCOS_LOG(TRACE) << LOG_BADGE("Precompiled") << LOG_DESC("wedpr_secp256k1_recover_public_key")
                    << LOG_KV("hash", *toHexString(msgHash.data, msgHash.data + msgHash.len))
                    << LOG_KV("rsv", *toHexString(rsv.data, rsv.data + rsv.len))
                    << LOG_KV("publicKey",
                           *toHexString(publicKey.data, publicKey.data + publicKey.len));
    auto retCode = wedpr_secp256k1_recover_public_key(&msgHash, &rsv, &publicKey);
    if (retCode != 0)
    {
        BCOS_LOG(TRACE) << LOG_BADGE("Precompiled") << LOG_DESC("ecRecover publicKey failed");
        return {true, {}};
    }
    BCOS_LOG(TRACE) << LOG_BADGE("Precompiled")
                    << LOG_DESC("wedpr_secp256k1_recover_public_key success");
    // keccak256 and set first 12 byte to zero
    CInputBuffer pubkeyBuffer{(const char*)publicKeyBytes.data(), PUBLIC_KEY_LENGTH};
    COutputBuffer pubkeyHash{(char*)ret.second.data(), crypto::HashType::SIZE};
    retCode = wedpr_keccak256_hash(&pubkeyBuffer, &pubkeyHash);
    if (retCode != 0)
    {
        return {true, {}};
    }
    memset(ret.second.data(), 0, 12);
    BCOS_LOG(TRACE) << LOG_BADGE("Precompiled") << LOG_DESC("ecRecover success");
    return ret;
}


}  // namespace crypto

}  // namespace bcos