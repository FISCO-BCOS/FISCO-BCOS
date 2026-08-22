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
#include "bcos-crypto/signature/secp256k1/Secp256k1Crypto.h"
#include "wedpr-crypto/WedprCrypto.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <evmone_precompiles/blake2b.hpp>
#include <evmone_precompiles/bls.hpp>
#include <evmone_precompiles/bn254.hpp>
#include <evmone_precompiles/kzg.hpp>
#include <evmone_precompiles/modexp.hpp>
#include <evmone_precompiles/ripemd160.hpp>
#include <evmone_precompiles/secp256r1.hpp>
#include <evmone_precompiles/sha256.hpp>
#include <intx/intx.hpp>
#include <span>
#include <bcos-utilities/BoostLog.h>

using namespace std;
using namespace bcos;
using namespace bcos::crypto;

namespace bcos::executor
{
PrecompiledRegistrar* PrecompiledRegistrar::get()
{
    static PrecompiledRegistrar instance;
    return &instance;
}

PrecompiledExecutor PrecompiledRegistrar::registerExecutor(
    std::string const& _name, PrecompiledExecutor const& _exec)
{
    return (get()->m_execs[_name] = _exec);
}

void PrecompiledRegistrar::unregisterExecutor(std::string const& _name)
{
    get()->m_execs.erase(_name);
}

PrecompiledPricer PrecompiledRegistrar::registerPricer(
    std::string const& _name, PrecompiledPricer const& _exec)
{
    return (get()->m_pricers[_name] = _exec);
}

void PrecompiledRegistrar::unregisterPricer(std::string const& _name)
{
    get()->m_pricers.erase(_name);
}

PrecompiledContract::PrecompiledContract(
    PrecompiledPricer const& _cost, PrecompiledExecutor const& _exec, u256 const& _startingBlock)
  : m_cost(_cost), m_execute(_exec), m_startingBlock(_startingBlock)
{}

PrecompiledContract::PrecompiledContract(
    unsigned _base, unsigned _word, PrecompiledExecutor const& _exec, u256 const& _startingBlock)
  : PrecompiledContract(
        [=](bytesConstRef _in) -> bigint {
            bigint size = _in.size();
            bigint base = _base;
            bigint word = _word;
            return base + (size + 31) / 32 * word;
        },
        _exec, _startingBlock)
{}

bigint PrecompiledContract::cost(bytesConstRef _in) const
{
    return m_cost(_in);
}

std::pair<bool, bytes> PrecompiledContract::execute(bytesConstRef _in) const
{
    return m_execute(_in);
}

u256 const& PrecompiledContract::startingBlock() const
{
    return m_startingBlock;
}

bcos::precompiled::Precompiled::Ptr bcos::executor::PrecompiledMap::at(std::string_view _key,
    uint32_t version, bool isAuth, ledger::Features const& features) const noexcept
{
    if (!_key.starts_with(precompiled::SYS_ADDRESS_PREFIX) && !_key.starts_with(tool::FS_SYS_BIN))
    {
        return nullptr;
    }
    auto it = m_map.find(std::string(_key));
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
    auto const it = get()->m_execs.find(_name);
    if (it == get()->m_execs.end())
    {
        BOOST_THROW_EXCEPTION(ExecutorNotFound());
    }
    return it->second;
}

PrecompiledPricer const& PrecompiledRegistrar::pricer(std::string const& _name)
{
    const auto it = get()->m_pricers.find(_name);
    if (it == get()->m_pricers.end())
    {
        BOOST_THROW_EXCEPTION(PricerNotFound());
    }
    return it->second;
}

}  // namespace bcos::executor

namespace bcos::precompiled
{

Precompiled::Precompiled(crypto::Hash::Ptr _hashImpl) : m_hashImpl(std::move(_hashImpl))
{
    assert(m_hashImpl);
    m_precompiledGasFactory = std::make_shared<PrecompiledGasFactory>();
    assert(m_precompiledGasFactory);
}

bool Precompiled::isParallelPrecompiled()
{
    return false;
}

std::vector<std::string> Precompiled::getParallelTag(bytesConstRef)
{
    return {};
}

}  // namespace bcos::precompiled

namespace
{
ETH_REGISTER_PRECOMPILED(ecrecover)(bytesConstRef _in)
{
    // When supported_version> = v2.4.0, ecRecover uniformly calls the ECDSA verification function
    return bcos::crypto::ecRecover(_in);
}

ETH_REGISTER_PRECOMPILED(sha256)(bytesConstRef _in)
{
    bytes output(evmone::crypto::SHA256_HASH_SIZE, 0);
    evmone::crypto::sha256(reinterpret_cast<std::byte*>(output.data()),
        reinterpret_cast<const std::byte*>(_in.data()), _in.size());
    return {true, std::move(output)};
}

ETH_REGISTER_PRECOMPILED(ripemd160)(bytesConstRef _in)
{
    bytes output(32, 0);
    evmone::crypto::ripemd160(reinterpret_cast<std::byte*>(output.data() + 12),
        reinterpret_cast<const std::byte*>(_in.data()), _in.size());
    return {true, std::move(output)};
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
    // EIP-198: big-number modular exponentiation (base^exp) % mod.
    // https://github.com/ethereum/EIPs/blob/master/EIPS/eip-198.md
    auto parseLen = [&](size_t offset) -> size_t {
        if (_in.size() < offset + 32)
            return 0;
        bigint v(parseBigEndianRightPadded(_in, offset, 32));
        return v > std::numeric_limits<size_t>::max() ? 0 : static_cast<size_t>(v);
    };
    size_t const baseLen = parseLen(0);
    size_t const expLen = parseLen(32);
    size_t const modLen = parseLen(64);

    // Safety net: gas pricer should prevent lengths beyond size_t::max()/8.
    // If these fire, the gas schedule has a bug and let an impossibly large
    // modexp through. See EIP-198 gas formula.
    assert(baseLen <= std::numeric_limits<size_t>::max() / 8);
    assert(expLen <= std::numeric_limits<size_t>::max() / 8);
    assert(modLen <= std::numeric_limits<size_t>::max() / 8);

    if (modLen == 0)
        return {true, {}};

    // Zero-pad inputs to declared lengths (EIP-198: missing bytes are right-padded
    // with zeros). Track consumed bytes from the data section rather than using
    // declared lengths as offsets — actual input may be shorter than declared.
    size_t const dataStart = 96;
    size_t const dataAvail = _in.size() > dataStart ? _in.size() - dataStart : 0;
    size_t consumed = 0;
    auto padded = [&](size_t len) -> bytes {
        bytes buf(len, 0);
        size_t const avail = consumed < dataAvail ? dataAvail - consumed : 0;
        size_t const actual = std::min(len, avail);
        if (actual > 0)
            std::memcpy(buf.data(), _in.data() + dataStart + consumed, actual);
        consumed += actual;
        return buf;
    };
    bytes const baseBuf = padded(baseLen);
    bytes const expBuf = padded(expLen);
    bytes const modBuf = padded(modLen);

    // EIP-198: if mod is zero, return all-zero output
    bool const modZero =
        std::all_of(modBuf.begin(), modBuf.end(), [](uint8_t b) { return b == 0; });
    if (modZero)
        return {true, bytes(modLen, 0)};

    bytes output(modLen, 0);
    evmone::crypto::modexp(std::span<const uint8_t>{baseBuf}, std::span<const uint8_t>{expBuf},
        std::span<const uint8_t>{modBuf}, output.data());
    return {true, std::move(output)};
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
    using namespace evmmax::bn254;

    uint8_t buf[128]{};
    std::memcpy(buf, _in.data(), std::min(_in.size(), sizeof(buf)));

    const auto p = AffinePoint::from_bytes(std::span<const uint8_t, 64>{buf, 64});
    const auto q = AffinePoint::from_bytes(std::span<const uint8_t, 64>{buf + 64, 64});
    if (!p.has_value() || !q.has_value() || !validate(*p) || !validate(*q))
        return {false, bytes(64, 0)};

    bytes output(64, 0);
    evmmax::ecc::add_affine(*p, *q).to_bytes(std::span<uint8_t, 64>{output.data(), 64});
    return {true, std::move(output)};
}

ETH_REGISTER_PRECOMPILED(alt_bn128_G1_mul)(bytesConstRef _in)
{
    using namespace evmmax::bn254;

    uint8_t buf[96]{};
    std::memcpy(buf, _in.data(), std::min(_in.size(), sizeof(buf)));

    const auto p = AffinePoint::from_bytes(std::span<const uint8_t, 64>{buf, 64});
    if (!p.has_value() || !validate(*p))
        return {false, bytes(64, 0)};

    const auto c = intx::be::unsafe::load<intx::uint256>(buf + 64);
    bytes output(64, 0);
    mul(*p, c).to_bytes(std::span<uint8_t, 64>{output.data(), 64});
    return {true, std::move(output)};
}

ETH_REGISTER_PRECOMPILED(alt_bn128_pairing_product)(bytesConstRef _in)
{
    static constexpr size_t PAIR_SIZE = 192;
    if (_in.size() % PAIR_SIZE != 0)
        return {false, bytes(32, 0)};

    using namespace evmmax::bn254;
    using intx::be::unsafe::load;

    std::vector<std::pair<Point, ExtPoint>> pairs;
    pairs.reserve(_in.size() / PAIR_SIZE);
    for (const uint8_t* ptr = _in.data(); ptr != _in.data() + _in.size(); ptr += PAIR_SIZE)
    {
        const auto g1 = AffinePoint::from_bytes(std::span<const uint8_t, 64>{ptr, 64});
        if (!g1.has_value() || !validate(*g1))
            return {false, bytes(32, 0)};

        const ExtPoint g2{{load<intx::uint256>(ptr + 96), load<intx::uint256>(ptr + 64)},
            {load<intx::uint256>(ptr + 160), load<intx::uint256>(ptr + 128)}};
        pairs.emplace_back(Point{g1->x.value(), g1->y.value()}, g2);
    }

    bytes output(32, 0);
    auto const result = pairing_check(pairs);
    if (!result.has_value())
        return {false, bytes(32, 0)};
    if (*result)
        output[31] = 1;
    return {true, std::move(output)};
}

ETH_REGISTER_PRECOMPILED_PRICER(alt_bn128_pairing_product)
(bytesConstRef _in)
{
    auto const k = _in.size() / 192;
    return 45000 + k * 34000;
}

ETH_REGISTER_PRECOMPILED(blake2_compression)(bytesConstRef _in)
{
    static constexpr size_t totalInputSize = 213;
    if (_in.size() != totalInputSize)
        return {false, {}};

    // EIP-152 §spec:
    //   rounds — 32-bit unsigned big-endian word
    //   h      — 8  unsigned 64-bit little-endian words (64 bytes)
    //   m      — 16 unsigned 64-bit little-endian words (128 bytes)
    //   t0, t1 — 2  unsigned 64-bit little-endian words (8 bytes each)
    //   f      — final block indicator flag (1 byte)
    //   Output: return the updated state vector h with unchanged encoding (little-endian)
    auto const rounds = fromBigEndian<uint32_t>(_in.getCroppedData(0, 4));
    uint64_t h[8]{};
    uint64_t m[16]{};
    uint64_t t[2]{};

    // Use std::memcpy to load little-endian words.  On all supported platforms
    // (x86-64, AArch64) native byte order is little-endian, so a direct memory
    // copy produces the correct integer value without any byte swapping.
    for (size_t i = 0; i < 8; ++i)
        std::memcpy(&h[i], _in.data() + 4 + i * 8, 8);
    for (size_t i = 0; i < 16; ++i)
        std::memcpy(&m[i], _in.data() + 68 + i * 8, 8);
    std::memcpy(&t[0], _in.data() + 196, 8);
    std::memcpy(&t[1], _in.data() + 204, 8);

    auto const finalBlockIndicator = _in[212];
    if (finalBlockIndicator != 0 && finalBlockIndicator != 1)
        return {false, {}};
    auto const last = finalBlockIndicator != 0;

    evmone::crypto::blake2b_compress(rounds, h, m, t, last);

    // Output h[] back as little-endian bytes (unchanged encoding per EIP-152).
    bytes output(64, 0);
    for (size_t i = 0; i < 8; ++i)
        std::memcpy(output.data() + i * 8, &h[i], 8);
    return {true, std::move(output)};
}

ETH_REGISTER_PRECOMPILED_PRICER(blake2_compression)
(bytesConstRef _in)
{
    auto const rounds = fromBigEndian<uint32_t>(_in.getCroppedData(0, 4));
    return rounds;
}

// The precompiled contract for point evaluation, EIP-4844:
// https://eips.ethereum.org/EIPS/eip-4844#point-evaluation-precompile
ETH_REGISTER_PRECOMPILED(point_evaluation)(bytesConstRef _in)
{
    static constexpr size_t versioned_hash_size = 32;
    static constexpr size_t z_end_bound = 64;
    static constexpr size_t y_end_bound = 96;
    static constexpr size_t commitment_end_bound = 144;
    static constexpr size_t proof_end_bound = 192;

    if (_in.size() != 192)
        return {false, {}};

    std::array<std::byte, evmone::crypto::SHA256_HASH_SIZE> expectedVersionedHash{};
    evmone::crypto::sha256(expectedVersionedHash.data(),
        reinterpret_cast<const std::byte*>(_in.data() + y_end_bound),
        commitment_end_bound - y_end_bound);
    expectedVersionedHash[0] = evmone::crypto::VERSIONED_HASH_VERSION_KZG;
    if (!std::equal(expectedVersionedHash.begin(), expectedVersionedHash.end(),
            reinterpret_cast<const std::byte*>(_in.data())))
        return {false, {}};

    bool ok = evmone::crypto::kzg_verify_proof(reinterpret_cast<const std::byte*>(_in.data()),
        reinterpret_cast<const std::byte*>(_in.data() + versioned_hash_size),
        reinterpret_cast<const std::byte*>(_in.data() + z_end_bound),
        reinterpret_cast<const std::byte*>(_in.data() + y_end_bound),
        reinterpret_cast<const std::byte*>(_in.data() + commitment_end_bound));
    if (!ok)
        return {false, {}};

    // Return FIELD_ELEMENTS_PER_BLOB and BLS_MODULUS as padded 32 byte big endian values
    // return turn and Bytes(U256(FIELD_ELEMENTS_PER_BLOB).to_be_bytes32() +
    // U256(BLS_MODULUS).to_be_bytes32()) refer to
    // https://github.com/erigontech/silkworm/blob/85ba5171e88855a6702602d38f102aae9b896f9c/silkworm/core/execution/precompile.cpp#L502-L524
    return {
        true, bcos::fromHex("000000000000000000000000000000000000000000000000000000000000100073eda"
                            "753299d7d483339d80809a1d80553bda402fffe5bfeffffffff00000001")};
}

ETH_REGISTER_PRECOMPILED_PRICER(point_evaluation)(bytesConstRef _in)
{
    return 50000;
}

// EIP-2537 BLS12-381 precompiles (Prague-gated via HostContext)

ETH_REGISTER_PRECOMPILED(bls12_g1add)(bytesConstRef _in)
{
    constexpr size_t INPUT_SIZE = 256;
    if (_in.size() != INPUT_SIZE)
        return {false, {}};
    std::array<uint8_t, INPUT_SIZE> in{};
    std::copy_n(_in.data(), INPUT_SIZE, in.data());
    std::array<uint8_t, 128> out{};
    bool const ok = evmone::crypto::bls::g1_add(
        out.data(), out.data() + 64, in.data(), in.data() + 64, in.data() + 128, in.data() + 192);
    if (!ok)
        return {false, {}};
    return {true, bytes(out.begin(), out.end())};
}
ETH_REGISTER_PRECOMPILED_PRICER(bls12_g1add)(bytesConstRef)
{
    return u256(375);
}

ETH_REGISTER_PRECOMPILED(bls12_g1msm)(bytesConstRef _in)
{
    constexpr size_t PAIR_SIZE = 160;
    if (_in.empty() || _in.size() % PAIR_SIZE != 0)
        return {false, {}};
    std::array<uint8_t, 128> out{};
    bool const ok =
        evmone::crypto::bls::g1_msm(out.data(), out.data() + 64, _in.data(), _in.size());
    if (!ok)
        return {false, {}};
    return {true, bytes(out.begin(), out.end())};
}
ETH_REGISTER_PRECOMPILED_PRICER(bls12_g1msm)(bytesConstRef _in)
{
    constexpr size_t PAIR_SIZE = 160;
    // EIP-2537: k = floor(len(input) / PAIR_SIZE). Only k == 0 (empty or too short for one pair)
    // returns zero gas. For k >= 1 the formula charges gas even if the input length is not
    // divisible — the precompile execution will reject malformed input, but gas is already
    // charged (matching go-ethereum behaviour).
    auto const k = _in.size() / PAIR_SIZE;
    if (k == 0)
        return u256(0);
    static constexpr uint16_t DISCOUNTS[] = {1000, 949, 848, 797, 764, 750, 738, 728, 719, 712, 705,
        698, 692, 687, 682, 677, 673, 669, 665, 661, 658, 654, 651, 648, 645, 642, 640, 637, 635,
        632, 630, 627, 625, 623, 621, 619, 617, 615, 613, 611, 609, 608, 606, 604, 603, 601, 599,
        598, 596, 595, 593, 592, 591, 589, 588, 586, 585, 584, 582, 581, 580, 579, 577, 576, 575,
        574, 573, 572, 570, 569, 568, 567, 566, 565, 564, 563, 562, 561, 560, 559, 558, 557, 556,
        555, 554, 553, 552, 551, 550, 549, 548, 547, 547, 546, 545, 544, 543, 542, 541, 540, 540,
        539, 538, 537, 536, 536, 535, 534, 533, 532, 532, 531, 530, 529, 528, 528, 527, 526, 525,
        525, 524, 523, 522, 522, 521, 520, 520, 519};
    // evmone caps MSM at 128 pairs; larger k means the gas pricer let through an invalid input.
    assert(k <= std::size(DISCOUNTS) && "BLS G1MSM: too many pairs for discount table");
    auto const discount = DISCOUNTS[std::min(k, std::size(DISCOUNTS)) - 1];
    return u256(12000 * static_cast<int64_t>(discount) * static_cast<int64_t>(k) / 1000);
}

ETH_REGISTER_PRECOMPILED(bls12_g2add)(bytesConstRef _in)
{
    constexpr size_t INPUT_SIZE = 512;
    if (_in.size() != INPUT_SIZE)
        return {false, {}};
    std::array<uint8_t, INPUT_SIZE> in{};
    std::copy_n(_in.data(), INPUT_SIZE, in.data());
    std::array<uint8_t, 256> out{};
    bool const ok = evmone::crypto::bls::g2_add(
        out.data(), out.data() + 128, in.data(), in.data() + 128, in.data() + 256, in.data() + 384);
    if (!ok)
        return {false, {}};
    return {true, bytes(out.begin(), out.end())};
}
ETH_REGISTER_PRECOMPILED_PRICER(bls12_g2add)(bytesConstRef)
{
    return u256(600);
}

ETH_REGISTER_PRECOMPILED(bls12_g2msm)(bytesConstRef _in)
{
    constexpr size_t PAIR_SIZE = 288;
    if (_in.empty() || _in.size() % PAIR_SIZE != 0)
        return {false, {}};
    std::array<uint8_t, 256> out{};
    bool const ok =
        evmone::crypto::bls::g2_msm(out.data(), out.data() + 128, _in.data(), _in.size());
    if (!ok)
        return {false, {}};
    return {true, bytes(out.begin(), out.end())};
}
ETH_REGISTER_PRECOMPILED_PRICER(bls12_g2msm)(bytesConstRef _in)
{
    constexpr size_t PAIR_SIZE = 288;
    // EIP-2537: k = floor(len(input) / PAIR_SIZE). Only k == 0 (empty or too short for one pair)
    // returns zero gas. For k >= 1 the formula charges gas even if the input length is not
    // divisible — the precompile execution will reject malformed input, but gas is already
    // charged (matching go-ethereum behaviour).
    auto const k = _in.size() / PAIR_SIZE;
    if (k == 0)
        return u256(0);
    static constexpr uint16_t DISCOUNTS[] = {1000, 1000, 923, 884, 855, 832, 812, 796, 782, 770,
        759, 749, 740, 732, 724, 717, 711, 704, 699, 693, 688, 683, 679, 674, 670, 666, 663, 659,
        655, 652, 649, 646, 643, 640, 637, 634, 632, 629, 627, 624, 622, 620, 618, 615, 613, 611,
        609, 607, 606, 604, 602, 600, 598, 597, 595, 593, 592, 590, 589, 587, 586, 584, 583, 582,
        580, 579, 578, 576, 575, 574, 573, 571, 570, 569, 568, 567, 566, 565, 563, 562, 561, 560,
        559, 558, 557, 556, 555, 554, 553, 552, 552, 551, 550, 549, 548, 547, 546, 545, 545, 544,
        543, 542, 541, 541, 540, 539, 538, 537, 537, 536, 535, 535, 534, 533, 532, 532, 531, 530,
        530, 529, 528, 528, 527, 526, 526, 525, 524, 524};
    // evmone caps MSM at 128 pairs; larger k means the gas pricer let through an invalid input.
    assert(k <= std::size(DISCOUNTS) && "BLS G2MSM: too many pairs for discount table");
    auto const discount = DISCOUNTS[std::min(k, std::size(DISCOUNTS)) - 1];
    return u256(22500 * static_cast<int64_t>(discount) * static_cast<int64_t>(k) / 1000);
}

ETH_REGISTER_PRECOMPILED(bls12_pairing_check)(bytesConstRef _in)
{
    constexpr size_t PAIR_SIZE = 384;
    if (_in.empty() || _in.size() % PAIR_SIZE != 0)
        return {false, {}};
    std::array<uint8_t, 32> out{};
    bool const ok = evmone::crypto::bls::pairing_check(out.data(), _in.data(), _in.size());
    if (!ok)
        return {false, {}};
    return {true, bytes(out.begin(), out.end())};
}
ETH_REGISTER_PRECOMPILED_PRICER(bls12_pairing_check)(bytesConstRef _in)
{
    constexpr size_t PAIR_SIZE = 384;
    // EIP-2537: k = floor(len(input) / PAIR_SIZE). Only k == 0 (empty or too short for one pair)
    // returns zero gas. For k >= 1 the formula charges gas even if the input length is not
    // divisible — the precompile execution will reject malformed input, but gas is already
    // charged (matching go-ethereum behaviour).
    auto const k = static_cast<int64_t>(_in.size() / PAIR_SIZE);
    if (k == 0)
        return u256(0);
    return u256(37700 + 32600 * k);
}

ETH_REGISTER_PRECOMPILED(bls12_map_fp_to_g1)(bytesConstRef _in)
{
    constexpr size_t INPUT_SIZE = 64;
    if (_in.size() != INPUT_SIZE)
        return {false, {}};
    std::array<uint8_t, INPUT_SIZE> in{};
    std::copy_n(_in.data(), INPUT_SIZE, in.data());
    std::array<uint8_t, 128> out{};
    bool const ok = evmone::crypto::bls::map_fp_to_g1(out.data(), out.data() + 64, in.data());
    if (!ok)
        return {false, {}};
    return {true, bytes(out.begin(), out.end())};
}
ETH_REGISTER_PRECOMPILED_PRICER(bls12_map_fp_to_g1)(bytesConstRef)
{
    return u256(5500);
}

ETH_REGISTER_PRECOMPILED(bls12_map_fp2_to_g2)(bytesConstRef _in)
{
    constexpr size_t INPUT_SIZE = 128;
    if (_in.size() != INPUT_SIZE)
        return {false, {}};
    std::array<uint8_t, INPUT_SIZE> in{};
    std::copy_n(_in.data(), INPUT_SIZE, in.data());
    std::array<uint8_t, 256> out{};
    bool const ok = evmone::crypto::bls::map_fp2_to_g2(out.data(), out.data() + 128, in.data());
    if (!ok)
        return {false, {}};
    return {true, bytes(out.begin(), out.end())};
}
ETH_REGISTER_PRECOMPILED_PRICER(bls12_map_fp2_to_g2)(bytesConstRef)
{
    return u256(23800);
}

// EIP-7212 / RIP-7212: secp256r1 (P-256) signature verification
// Input: 160 bytes = msg_hash(32) ++ r(32) ++ s(32) ++ x(32) ++ y(32)
// Output: 32 bytes with last byte 0x01 on success, empty on wrong-size or failed verify
// Address: 0x0100 (Osaka-gated via callBuiltInPrecompiled guard)
ETH_REGISTER_PRECOMPILED(p256verify)(bytesConstRef _in)
{
    static constexpr size_t INPUT_SIZE = 160;
    if (_in.size() != INPUT_SIZE)
        return {true, {}};  // EIP-7212: wrong size → success with empty output
    const auto* d = _in.data();
    ethash::hash256 h{};
    std::memcpy(h.bytes, d, 32);
    const auto r = intx::be::unsafe::load<intx::uint256>(d + 32);
    const auto s = intx::be::unsafe::load<intx::uint256>(d + 64);
    const auto qx = intx::be::unsafe::load<intx::uint256>(d + 96);
    const auto qy = intx::be::unsafe::load<intx::uint256>(d + 128);
    bool ok = evmmax::secp256r1::verify(h, r, s, qx, qy);
    if (!ok)
        return {true, {}};  // EIP-7212: invalid signature → success with empty output
    bytes output(32, 0);
    output[31] = 1;
    return {true, std::move(output)};
}

ETH_REGISTER_PRECOMPILED_PRICER(p256verify)(bytesConstRef)
{
    return u256(6900);  // EIP-7212 / evmone 0.21 gas cost
}

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
                    << LOG_KV("rsv", toHex(bytesConstRef(rawRSV, RSV_LENGTH)));
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


}  // namespace crypto

}  // namespace bcos
