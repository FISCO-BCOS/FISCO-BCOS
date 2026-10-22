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
 * @brief Curve25519 (Ristretto255) VRF, byte-compatible with WeDPR-Lab-Crypto's
 * wedpr_curve25519_vrf_* family (curve25519-dalek 1.2.6 semantics). Pure C++
 * reimplementation over the vendored libsodium ref10 ristretto255 code.
 * @file Curve25519Vrf.h
 */
#pragma once

#include <bcos-utilities/Common.h>

namespace bcos::crypto
{
inline constexpr size_t CURVE25519_VRF_PUBLIC_KEY_SIZE = 32;
inline constexpr size_t CURVE25519_VRF_PROOF_SIZE = 96;
inline constexpr size_t CURVE25519_VRF_HASH_SIZE = 32;

// public key = x·G1 with x = SHA3-512(secret) reduced mod l (wide reduce), SEC-encoded as a
// compressed Ristretto255 point; the secret may be arbitrary bytes. Returns an empty bytes
// object on failure.
bcos::bytes curve25519VrfDerivePublicKey(bytesConstRef _secretKey);

// 96-byte proof: gamma ‖ c ‖ s (c, s little-endian scalars). The nonce is random
// (64 random bytes wide-reduced, like dalek Scalar::random), so proofs are non-deterministic.
// Returns an empty bytes object on failure.
bcos::bytes curve25519VrfProve(bytesConstRef _secretKey, bytesConstRef _input);

// never throws: any malformed input (wrong lengths, undecodable points) yields false; the
// identity public key is valid input
bool curve25519VrfVerify(bytesConstRef _publicKey, bytesConstRef _input, bytesConstRef _proof);

// Keccak-256(encode(8·gamma)); returns an empty bytes object if the proof is malformed
bcos::bytes curve25519VrfProofToHash(bytesConstRef _proof);

// 32 bytes && valid Ristretto255 encoding (the identity counts as valid)
bool curve25519VrfIsValidPublicKey(bytesConstRef _publicKey);
}  // namespace bcos::crypto
