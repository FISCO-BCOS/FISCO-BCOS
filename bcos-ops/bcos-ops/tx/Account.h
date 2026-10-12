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
 * @brief the signing account: a fresh random key by default, or --account <pem>
 * @file Account.h
 */
#pragma once

#include <bcos-crypto/interfaces/crypto/KeyPairInterface.h>
#include <optional>
#include <string>

namespace bcos::ops
{
/// reads an unencrypted EC private key pem (the format build_chain / get_account produce)
bytes loadPemPrivateKey(std::string const& _path);
/// a private key file: PEM when the content starts with "-----BEGIN", otherwise 64 hex
/// characters with an optional 0x prefix (surrounding whitespace ignored). Throws OpsError{1}.
bytes loadPrivateKey(std::string const& _path);
/// random when _keyPath is empty; Secp256k1 or SM2 by _sm; _keyPath as loadPrivateKey
bcos::crypto::KeyPairInterface::UniquePtr makeAccount(
    bool _sm, std::optional<std::string> const& _keyPath);
/// 0x-prefixed address: Keccak256 of the public key for secp256k1, SM3 for SM2
std::string accountAddress(bool _sm, bcos::crypto::KeyPairInterface& _keyPair);

struct PemKeyPairFiles
{
    std::string privateKeyFile;  // <dir>/<0xaddr>.pem
    std::string publicKeyFile;   // <dir>/<0xaddr>.public.pem
};
/// writes the key pair in console's accounts/ layout (unencrypted EC private key pem + public
/// key pem, named by the address); creates _dir when missing. Throws OpsError{1} on I/O failure.
PemKeyPairFiles writePemKeyPair(
    std::string const& _dir, bool _sm, bcos::crypto::KeyPairInterface& _keyPair);
}  // namespace bcos::ops
