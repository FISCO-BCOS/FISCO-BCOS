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
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-crypto/signature/codec/SignatureDataWithV.h"
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "wedpr-crypto/WedprBn128.h"
#include "wedpr-crypto/WedprCrypto.h"
#include <bcos-utilities/Log.h>
#include <algorithm>
#if _MSC_VER
#define ALWAYS_INLINE __forceinline
#elif __has_attribute(always_inline)
#define ALWAYS_INLINE __attribute__((always_inline))
#else
#define ALWAYS_INLINE
#endif
#define CHUNK_SIZE 64
#define TOTAL_LEN_LEN 8

using namespace std;
using namespace bcos;
using namespace bcos::crypto;

namespace bcos::executor
{
PrecompiledRegistrar* PrecompiledRegistrar::s_this = nullptr;

bcos::precompiled::Precompiled::Ptr bcos::executor::PrecompiledMap::at(std::string const& _key,
    uint32_t version, bool isAuth, ledger::Features const& features) const noexcept
{
    if (!_key.starts_with(precompiled::SYS_ADDRESS_PREFIX) && !_key.starts_with(tool::FS_SYS_BIN))
    {
        return nullptr;
    }
    auto it = m_map.find(_key);
    if (it == m_map.end())
    {
        return nullptr;
    }
    if (it->second.availableFunc(version, isAuth, features))
    {
        return it->second.precompiled;
    }
    return nullptr;
}
bool bcos::executor::PrecompiledMap::contains(std::string const& key, uint32_t version, bool isAuth,
    ledger::Features const& features) const noexcept
{
    return at(key, version, isAuth, features) != nullptr;
}

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

}  // namespace bcos::executor

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

// ETH_REGISTER_PRECOMPILED(point_evaluation)(bytesConstRef _in)
//{
//     static constexpr size_t versioned_hash_size = 32;
//     static constexpr size_t z_end_bound = 64;
//     static constexpr size_t y_end_bound = 96;
//     static constexpr size_t commitment_end_bound = 144;
//     static constexpr size_t proof_end_bound = 192;
//
//     if(_in.size() != 192)
//         return {false, {}};
//
//     auto const versioned_hash = _in.getCroppedData(0, versioned_hash_size);
//     auto const z = _in.getCroppedData(versioned_hash_size, z_end_bound);
//     auto const y = _in.getCroppedData(z_end_bound, y_end_bound);
//     auto const commitment = _in.getCroppedData(y_end_bound, commitment_end_bound);
//     auto const proof = _in.getCroppedData(commitment_end_bound, proof_end_bound);
//
//     if(kzg2VersionedHash(commitment) != versioned_hash)
//         return {false, {}};
//
//     if(!verifyKZGProof(proof, commitment, z, y))
//         return {false, {}};
//
// }
//
// ETH_REGISTER_PRECOMPILED_PRICER(point_evaluation)(bytesConstRef _in)
//{
//     return 45000;
// }

}  // namespace

namespace bcos
{
namespace precompiled
{
}  // namespace precompiled


namespace crypto
{
// add sha2 -- sha256 to this file begin
h256 sha256(bytesConstRef _in) noexcept
{
    h256 ret;
    CInputBuffer in{(const char*)_in.data(), _in.size()};
    COutputBuffer result{(char*)ret.data(), h256::SIZE};
    if (wedpr_sha256_hash(&in, &result) != 0) [[unlikely]]
    {
        BCOS_LOG(TRACE) << LOG_BADGE("Precompiled") << LOG_DESC("sha256 failed.") << _in.toString();
        return ret;
    }
    return ret;
}

h160 ripemd160(bytesConstRef _in)
{
    h160 ret;
    CInputBuffer in{(const char*)_in.data(), _in.size()};
    COutputBuffer result{(char*)ret.data(), h160::SIZE};
    if (wedpr_ripemd160_hash(&in, &result) != 0) [[unlikely]]
    {
        BCOS_LOG(TRACE) << LOG_BADGE("Precompiled") << LOG_DESC("ripemd160 failed.")
                        << _in.toString();
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

// struct buffer_state
//{
//     const void* p;
//     size_t len;
//     size_t total_len;
//     bool single_one_delivered;
//     bool total_len_delivered;
// };
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
{                                // _in is hash(32),v(32),r(32),s(32), return address
    if (_in.size() <= 128 - 32)  // must has hash(32),v(32),r(32),s(32)
    {
        BCOS_LOG(TRACE) << LOG_BADGE("Precompiled")
                        << LOG_DESC("ecRecover: must has hash(32),v(32),r(32),s(32)");
        return {true, {}};
    }

    BCOS_LOG(TRACE) << LOG_BADGE("Precompiled") << LOG_DESC("ecRecover: ") << _in.size();
    byte rawRSV[RSV_LENGTH] = {0};
    memcpy(rawRSV, _in.data() + 64, std::min(_in.size() - 64, (size_t)(RSV_LENGTH - 1)));
    rawRSV[RSV_LENGTH - 1] = (byte)((int)_in[63] - 27);
    crypto::HashType mHash;
    memcpy(mHash.data(), _in.data(), crypto::HashType::SIZE);

    PublicPtr pk;
    try
    {
        pk = crypto::secp256k1Recover(mHash, bytesConstRef(rawRSV, RSV_LENGTH));
    }
    catch (...)
    {
        // is also ok and return 0x0000000000000000000000000000000000000084
        return {true, {}};
    }

    pair<bool, bytes> ret{true, bytes(crypto::HashType::SIZE, 0)};
    BCOS_LOG(TRACE) << LOG_BADGE("Precompiled") << LOG_DESC("wedpr_secp256k1_recover_public_key")
                    << LOG_KV("hash", toHexStringWithPrefix(mHash))
                    << LOG_KV("rsv", *toHexString(rawRSV, rawRSV + RSV_LENGTH));
    if (pk == nullptr)
    {
        BCOS_LOG(TRACE) << LOG_BADGE("Precompiled") << LOG_DESC("ecRecover publicKey failed");
        return {true, {}};
    }
    BCOS_LOG(TRACE) << LOG_BADGE("Precompiled")
                    << LOG_DESC("wedpr_secp256k1_recover_public_key success");
    // keccak256 and set first 12 byte to zero
    CInputBuffer pubkeyBuffer{pk->constData(), PUBLIC_KEY_LENGTH};
    COutputBuffer pubkeyHash{(char*)ret.second.data(), crypto::HashType::SIZE};
    auto retCode = wedpr_keccak256_hash(&pubkeyBuffer, &pubkeyHash);
    if (retCode != 0)
    {
        return {true, {}};
    }
    memset(ret.second.data(), 0, 12);
    BCOS_LOG(TRACE) << LOG_BADGE("Precompiled") << LOG_DESC("ecRecover success");
    return ret;
}

// static const uint32_t k[] = {
//     0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
//     0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
//     0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
//     0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
//     0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
//     0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
//     0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
//     0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
//     0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
//     0xc67178f2};
//
// static void init_buf_state(struct buffer_state* state, const void* input, size_t len) {
//     state->p = input;
//     state->len = len;
//     state->total_len = len;
//     state->single_one_delivered = false;
//     state->total_len_delivered = false;
// }
//
// static bool calc_chunk(uint8_t chunk[CHUNK_SIZE], struct buffer_state* state) {
//     if (state->total_len_delivered) {
//         return false;
//     }
//
//     if (state->len >= CHUNK_SIZE) {
//         memcpy(chunk, state->p, CHUNK_SIZE);
//         state->p += CHUNK_SIZE;
//         state->len -= CHUNK_SIZE;
//         return true;
//     }
//
//     size_t space_in_chunk = CHUNK_SIZE - state->len;
//     if (state->len) {  // avoid adding 0 to nullptr
//         memcpy(chunk, state->p, state->len);
//         chunk += state->len;
//         state->p += state->len;
//     }
//     state->len = 0;
//
//     /* If we are here, space_in_chunk is one at minimum. */
//     if (!state->single_one_delivered) {
//         *chunk++ = 0x80;
//         space_in_chunk -= 1;
//         state->single_one_delivered = true;
//     }
//
//     /*
//      * Now:
//      * - either there is enough space left for the total length, and we can conclude,
//      * - or there is too little space left, and we have to pad the rest of this chunk with
//      zeroes.
//      * In the latter case, we will conclude at the next invocation of this function.
//      */
//     if (space_in_chunk >= TOTAL_LEN_LEN) {
//         const size_t left = space_in_chunk - TOTAL_LEN_LEN;
//         size_t len = state->total_len;
//         int i = 0;
//         memset(chunk, 0x00, left);
//         chunk += left;
//
//         /* Storing of len * 8 as a big endian 64-bit without overflow. */
//         chunk[7] = (uint8_t)(len << 3);
//         len >>= 5;
//         for (i = 6; i >= 0; i--) {
//             chunk[i] = (uint8_t)len;
//             len >>= 8;
//         }
//         state->total_len_delivered = true;
//     } else {
//         memset(chunk, 0x00, space_in_chunk);
//     }
//
//     return true;
// }
//
// static inline ALWAYS_INLINE void sha_256_implementation(uint32_t h[8], const void* input, size_t
// len){
//     struct buffer_state state;
//     init_buf_state(&state, input, len);
//
//     /* 512-bit chunks is what we will operate on. */
//     uint8_t chunk[CHUNK_SIZE];
//
//     while (calc_chunk(chunk, &state)) {
//         unsigned i = 0, j = 0;
//
//         uint32_t ah[8];
//         /* Initialize working variables to current hash value: */
//         for (i = 0; i < 8; i++) {
//             ah[i] = h[i];
//         }
//
//         const uint8_t* p = chunk;
//
//         uint32_t w[16];
//
//         /* Compression function main loop: */
//         for (i = 0; i < 4; i++) {
//             for (j = 0; j < 16; j++) {
//                 if (i == 0) {
//                     w[j] = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 |
//                     (uint32_t)p[3]; p += 4;
//                 } else {
//                     /* Extend the first 16 words into the remaining 48 words w[16..63] of the
//                     message schedule array: */ const uint32_t s0 =
//                         rotr64(w[(j + 1) & 0xf], 7) ^ rotr64(w[(j + 1) & 0xf], 18) ^ (w[(j + 1) &
//                         0xf] >> 3);
//                     const uint32_t s1 =
//                         rotr64(w[(j + 14) & 0xf], 17) ^ rotr64(w[(j + 14) & 0xf], 19) ^ (w[(j +
//                         14) & 0xf] >> 10);
//                     w[j] = w[j] + s0 + w[(j + 9) & 0xf] + s1;
//                 }
//                 const uint32_t s1 = rotr64(ah[4], 6) ^ rotr64(ah[4], 11) ^ rotr64(ah[4], 25);
//                 const uint32_t ch = (ah[4] & ah[5]) ^ (~ah[4] & ah[6]);
//                 const uint32_t temp1 = ah[7] + s1 + ch + k[i << 4 | j] + w[j];
//                 const uint32_t s0 = rotr64(ah[0], 2) ^ rotr64(ah[0], 13) ^ rotr64(ah[0], 22);
//                 const uint32_t maj = (ah[0] & ah[1]) ^ (ah[0] & ah[2]) ^ (ah[1] & ah[2]);
//                 const uint32_t temp2 = s0 + maj;
//
//                 ah[7] = ah[6];
//                 ah[6] = ah[5];
//                 ah[5] = ah[4];
//                 ah[4] = ah[3] + temp1;
//                 ah[3] = ah[2];
//                 ah[2] = ah[1];
//                 ah[1] = ah[0];
//                 ah[0] = temp1 + temp2;
//             }
//         }
//
//         /* Add the compressed chunk to the current hash value: */
//         for (i = 0; i < 8; i++) {
//             h[i] += ah[i];
//         }
//     }
// }
//
// static void sha_256_generic(uint32_t h[8], const void* input, size_t len) {
// sha_256_implementation(h, input, len); } static void (*sha_256_best)(uint32_t h[8], const void*
// input, size_t len) = sha_256_generic;
//
//
//
// void silkworm_sha256(uint8_t hash[32], const uint8_t* input, size_t len, bool
// use_cpu_extensions){
//     uint32_t h[] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c,
//     0x1f83d9ab, 0x5be0cd19};
//
//     if (use_cpu_extensions) {
//         sha_256_best(h, input, len);
//     } else {
//         sha_256_generic(h, input, len);
//     }
//
//     /* Produce the final hash value (big-endian): */
//     for (unsigned i = 0, j = 0; i < 8; i++) {
//         hash[j++] = (uint8_t)(h[i] >> 24);
//         hash[j++] = (uint8_t)(h[i] >> 16);
//         hash[j++] = (uint8_t)(h[i] >> 8);
//         hash[j++] = (uint8_t)h[i];
//     }
// }
//
// evmc::bytes32 kzg2VersionedHash(bytesConstRef input){
//     evmc::bytes32 hash;
//     silkworm_sha256(hash.bytes, input.data(), input.size(), true);
//     hash.bytes[0] = bcos::executor::kBlobCommitmentVersionKzg;
//     return hash;
// }
//
// using G1 = blst_p1;
// using G2 = blst_p2;
// using Fr = blst_fr;
//
// bool verifyKZGProof(bytesConstRef commitment, bytesConstRef z, bytesConstRef y, bytesConstRef
// proof){
//     Fr z_fr, y_fr;
//     G1 commitment_g1, proof_g1;
//    return verifyKZGProofImpl(commitment, z, y, proof);
//
// }
//
// static bool verifyKZGProofImpl(bytesConstRef commitment, bytesConstRef z, bytesConstRef y,
// bytesConstRef proof){
//     return true;
// }

}  // namespace crypto

}  // namespace bcos