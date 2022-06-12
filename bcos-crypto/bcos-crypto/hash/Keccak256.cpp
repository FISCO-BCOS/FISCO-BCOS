/**
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
 * @brief Hash algorithm of keccak256
 * @file Keccak256.cpp
 */
#include <bcos-crypto/hash/Keccak256.h>
#include <wedpr-crypto/WedprCrypto.h>

using namespace bcos;
using namespace bcos::crypto;
HashType bcos::crypto::keccak256Hash(bytesConstRef _data)
{
    bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher hasher;
    hasher.update(_data);

    HashType out;
    hasher.final(out);
    return out;
}

HashType Keccak256::hash(bytesConstRef _data)
{
    return keccak256Hash(_data);
}

bcos::crypto::hasher::AnyHasher Keccak256::hasher()
{
    return bcos::crypto::hasher::AnyHasher{
        bcos::crypto::hasher::openssl::OpenSSL_Keccak256_Hasher{}};
}