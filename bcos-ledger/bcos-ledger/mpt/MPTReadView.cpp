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
 * @file MPTReadView.cpp
 * @brief Read-only account lookups over an MPT state root (spec §5.6, §7.1)
 */
#include "MPTReadView.h"
// AnyHasher.h defines the free bcos::crypto::hasher::hash(); OpenSSLHasher.h only defines the
// hasher type. A unity build can mask a missing include of the former (a sibling TU's include
// leaks in), so keep both explicit.
#include <bcos-crypto/hasher/AnyHasher.h>
#include <bcos-crypto/hasher/OpenSSLHasher.h>

namespace bcos::ledger::mpt
{

bcos::h256 accountKeyHash(bcos::Address const& addr)
{
    bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher hasher;
    bcos::h256 out;
    bcos::crypto::hasher::hash(hasher, addr.ref(), out);
    return out;
}

}  // namespace bcos::ledger::mpt
