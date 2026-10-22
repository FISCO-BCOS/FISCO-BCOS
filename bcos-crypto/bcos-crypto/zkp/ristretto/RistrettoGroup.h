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
 * @brief Ristretto255 group/scalar helpers matching curve25519-dalek semantics,
 * built on the PUBLIC libsodium API (vcpkg unofficial-sodium). Points are always
 * handled in their 32-byte compressed encoding — the representation every public
 * libsodium ristretto255 entry point takes and returns.
 * @file RistrettoGroup.h
 */
#pragma once

#include <sodium.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace bcos::crypto
{
inline constexpr size_t RISTRETTO_POINT_LEN = 32;
inline constexpr size_t RISTRETTO_SCALAR_LEN = 32;
using RistrettoPointBytes = std::array<std::uint8_t, RISTRETTO_POINT_LEN>;
using RistrettoScalarBytes = std::array<std::uint8_t, RISTRETTO_SCALAR_LEN>;

// the identity element's canonical encoding (32 zero bytes)
inline constexpr RistrettoPointBytes RISTRETTO_IDENTITY{};

namespace detail
{
inline void ensureSodiumInit()
{
    // thread-safe one-time initialisation (magic static); the ristretto255 math functions
    // do not strictly require sodium_init(), but it is cheap insurance
    static int const rc = sodium_init();
    (void)rc;
}

inline bool isZeroEncoding(const std::uint8_t* _bytes)
{
    std::uint8_t acc = 0;
    for (size_t i = 0; i < RISTRETTO_POINT_LEN; i++)
    {
        acc |= _bytes[i];
    }
    return acc == 0;
}
}  // namespace detail

// validity of a compressed Ristretto255 encoding (canonical, on the main subgroup).
// crypto_core_ristretto255_is_valid_point is exactly ristretto255_frombytes(...) == 0, the
// same predicate as curve25519-dalek's CompressedRistretto::decompress — and like dalek it
// ACCEPTS the identity encoding (32 zero bytes); verified empirically on libsodium 1.0.21
// and pinned down by unit tests.
inline bool ristrettoIsValidPoint(const std::uint8_t* _encoded)
{
    detail::ensureSodiumInit();
    return crypto_core_ristretto255_is_valid_point(_encoded) == 1;
}

inline bool ristrettoIsIdentity(const RistrettoPointBytes& _point)
{
    return detail::isZeroEncoding(_point.data());
}

// point addition/subtraction. libsodium's add/sub return 0 and produce the canonical
// identity encoding for identity results (verified empirically on 1.0.21: add(B, -B),
// add(B, 0), sub(B, B) all succeed), so they already carry dalek semantics; only invalid
// input encodings fail, and those cannot reach this layer (points are validated at decode)
inline RistrettoPointBytes ristrettoAdd(
    const RistrettoPointBytes& _a, const RistrettoPointBytes& _b)
{
    detail::ensureSodiumInit();
    RistrettoPointBytes result;
    int const rc = crypto_core_ristretto255_add(result.data(), _a.data(), _b.data());
    (void)rc;
    return result;
}

inline RistrettoPointBytes ristrettoSub(
    const RistrettoPointBytes& _a, const RistrettoPointBytes& _b)
{
    detail::ensureSodiumInit();
    RistrettoPointBytes result;
    int const rc = crypto_core_ristretto255_sub(result.data(), _a.data(), _b.data());
    (void)rc;
    return result;
}

// scalar·point with dalek semantics: a zero scalar, an identity input point, or an identity
// result all yield the identity encoding (dalek computes 0·P = x·identity = identity).
// libsodium reports -1 for exactly those cases (and zeroes the output); map -1 to the
// identity encoding. Genuinely invalid points also yield -1, but they cannot reach this
// layer — every point is validated at decode time by the callers.
inline RistrettoPointBytes ristrettoScalarmult(
    const RistrettoScalarBytes& _scalar, const RistrettoPointBytes& _point)
{
    detail::ensureSodiumInit();
    RistrettoPointBytes result = RISTRETTO_IDENTITY;
    int const rc = crypto_scalarmult_ristretto255(result.data(), _scalar.data(), _point.data());
    (void)rc;
    return result;
}

// scalar·G1 (canonical basepoint); zero scalar yields the identity encoding (see above)
inline RistrettoPointBytes ristrettoScalarmultBase(const RistrettoScalarBytes& _scalar)
{
    detail::ensureSodiumInit();
    RistrettoPointBytes result = RISTRETTO_IDENTITY;
    int const rc = crypto_scalarmult_ristretto255_base(result.data(), _scalar.data());
    (void)rc;
    return result;
}

// hash-to-curve: the same two-Elligator-map construction as dalek
// RistrettoPoint::from_uniform_bytes (byte-identical, pinned by the VRF golden vectors)
inline RistrettoPointBytes ristrettoFromHash(const std::uint8_t* _hash64)
{
    detail::ensureSodiumInit();
    RistrettoPointBytes result;
    crypto_core_ristretto255_from_hash(result.data(), _hash64);
    return result;
}

// reduces a little-endian integer (at most 64 bytes) mod l, like dalek
// Scalar::from_bytes_mod_order(_wide)
inline RistrettoScalarBytes ristrettoScalarReduce(const std::uint8_t* _bytes, size_t _len)
{
    detail::ensureSodiumInit();
    std::array<std::uint8_t, 64> wide{};
    std::memcpy(wide.data(), _bytes, std::min(_len, wide.size()));
    RistrettoScalarBytes scalar;
    crypto_core_ristretto255_scalar_reduce(scalar.data(), wide.data());
    return scalar;
}

// decodes a serialized scalar like dalek Scalar::from_bits: the 32-byte little-endian
// integer with bit 255 cleared, reduced mod l
inline RistrettoScalarBytes ristrettoScalarFromBits(const std::uint8_t* _bytes)
{
    std::array<std::uint8_t, RISTRETTO_SCALAR_LEN> masked;
    std::memcpy(masked.data(), _bytes, RISTRETTO_SCALAR_LEN);
    masked.back() &= 0x7f;
    return ristrettoScalarReduce(masked.data(), masked.size());
}

inline RistrettoScalarBytes ristrettoScalarMul(
    const RistrettoScalarBytes& _a, const RistrettoScalarBytes& _b)
{
    detail::ensureSodiumInit();
    RistrettoScalarBytes product;
    crypto_core_ristretto255_scalar_mul(product.data(), _a.data(), _b.data());
    return product;
}

inline RistrettoScalarBytes ristrettoScalarAdd(
    const RistrettoScalarBytes& _a, const RistrettoScalarBytes& _b)
{
    detail::ensureSodiumInit();
    RistrettoScalarBytes sum;
    crypto_core_ristretto255_scalar_add(sum.data(), _a.data(), _b.data());
    return sum;
}

inline RistrettoScalarBytes ristrettoScalarSub(
    const RistrettoScalarBytes& _a, const RistrettoScalarBytes& _b)
{
    detail::ensureSodiumInit();
    RistrettoScalarBytes difference;
    crypto_core_ristretto255_scalar_sub(difference.data(), _a.data(), _b.data());
    return difference;
}

// -a mod l
inline RistrettoScalarBytes ristrettoScalarNegate(const RistrettoScalarBytes& _a)
{
    detail::ensureSodiumInit();
    RistrettoScalarBytes negated;
    crypto_core_ristretto255_scalar_negate(negated.data(), _a.data());
    return negated;
}

// s = a * b + c (mod l)
inline RistrettoScalarBytes ristrettoScalarMulAdd(
    const RistrettoScalarBytes& _a, const RistrettoScalarBytes& _b, const RistrettoScalarBytes& _c)
{
    return ristrettoScalarAdd(ristrettoScalarMul(_a, _b), _c);
}

struct RistrettoMsmTerm
{
    RistrettoScalarBytes scalar;
    RistrettoPointBytes point;
};

// naive multiscalar multiplication (verify-path performance is non-critical)
inline RistrettoPointBytes ristrettoMultiscalarMul(const std::vector<RistrettoMsmTerm>& _terms)
{
    RistrettoPointBytes accumulator = RISTRETTO_IDENTITY;
    bool first = true;
    for (auto const& term : _terms)
    {
        auto product = ristrettoScalarmult(term.scalar, term.point);
        accumulator = first ? product : ristrettoAdd(accumulator, product);
        first = false;
    }
    return accumulator;
}

// point equality is compressed-byte equality, as in curve25519-dalek
inline bool ristrettoEqual(const RistrettoPointBytes& _a, const std::uint8_t* _b)
{
    return std::memcmp(_a.data(), _b, RISTRETTO_POINT_LEN) == 0;
}
}  // namespace bcos::crypto
