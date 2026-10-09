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
/// random when _pemPath is empty; Secp256k1 or SM2 by _sm
bcos::crypto::KeyPairInterface::UniquePtr makeAccount(
    bool _sm, std::optional<std::string> const& _pemPath);
}  // namespace bcos::ops
