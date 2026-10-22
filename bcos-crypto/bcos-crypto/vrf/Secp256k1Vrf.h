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
 * @brief secp256k1 VRF, byte-compatible with WeDPR-Lab-Crypto's
 * wedpr_secp256k1_vrf_* family (k256 0.13.4 semantics). Pure C++
 * reimplementation over libsecp256k1 and OpenSSL SHA-256.
 * @file Secp256k1Vrf.h
 */
#pragma once

#include <bcos-utilities/Common.h>

namespace bcos::crypto
{
inline constexpr size_t SECP256K1_VRF_SECRET_KEY_SIZE = 32;
inline constexpr size_t SECP256K1_VRF_PUBLIC_KEY_SIZE = 33;
inline constexpr size_t SECP256K1_VRF_PROOF_SIZE = 97;
inline constexpr size_t SECP256K1_VRF_HASH_SIZE = 32;

// SEC1-compressed public key (33 bytes) for a 32-byte big-endian secret scalar
// (0 < x < n). Returns an empty bytes object on failure. Note: wedpr returned SUCCESS with
// uninitialized-garbage output for invalid secrets; this implementation fails cleanly
// instead — no caller ever passes an invalid secret (keypair secrets are always valid).
bcos::bytes secp256k1VrfDerivePublicKey(bytesConstRef _secretKey);

// 97-byte proof: compressed gamma ‖ c ‖ s (c, s big-endian scalars). Fully deterministic
// (nonce k = SHA-256(secret), message-independent). Returns an empty bytes object on failure.
bcos::bytes secp256k1VrfProve(bytesConstRef _secretKey, bytesConstRef _input);

// never throws: any malformed input (wrong proof length, undecodable points, scalars >= n)
// yields false
bool secp256k1VrfVerify(bytesConstRef _publicKey, bytesConstRef _input, bytesConstRef _proof);

// SHA-256(compressed(8·gamma)); returns an empty bytes object if the proof is malformed
bcos::bytes secp256k1VrfProofToHash(bytesConstRef _proof);

// SEC1 compressed/uncompressed decode + on-curve check (the identity is rejected)
bool secp256k1VrfIsValidPublicKey(bytesConstRef _publicKey);
}  // namespace bcos::crypto
