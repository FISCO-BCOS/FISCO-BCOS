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
 * @file Secp256k1Ecdh.h
 * @brief secp256k1 ECDH shared-secret helpers (libsecp256k1) — needed by the
 *        RLPx ECIES handshake (raw-x shared secret + SHA-256 KDF).
 * @date 2026/8/18
 */
#pragma once

#include <bcos-utilities/Common.h>

namespace bcos::crypto
{
// ECDH shared secret (32 bytes) between `_publicKey` (64-byte uncompressed
// point without the 0x04 prefix, the FISCO convention) and `_privateKey`
// (32 bytes). The hash function selects the output:
//   - secp256k1EcdhCopyX: raw x-coordinate (RLPx ECIES KDF input)
//   - secp256k1EcdhSha256: sha256(compressedPoint || x) (SEC1 default)
bcos::bytes secp256k1EcdhCopyX(bytesConstRef _publicKey, bytesConstRef _privateKey);
bcos::bytes secp256k1EcdhSha256(bytesConstRef _publicKey, bytesConstRef _privateKey);
}  // namespace bcos::crypto
