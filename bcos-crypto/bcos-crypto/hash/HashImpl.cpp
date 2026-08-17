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
 */

#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/hash/Sha256.h>
#include <bcos-crypto/hash/Sha3.h>
#include <bcos-crypto/hasher/OpenSSLHasher.h>

namespace bcos::crypto
{
HashType keccak256Hash(bytesConstRef _data)
{
    hasher::openssl::OpenSSL_Keccak256_Hasher hasher;
    hasher.update(_data);

    HashType out;
    hasher.final(out);
    return out;
}

Keccak256::Keccak256()
{
    setHashImplType(HashImplType::Keccak256Hash);
}

HashType Keccak256::hash(bytesConstRef _data) const
{
    return keccak256Hash(_data);
}

bcos::crypto::hasher::AnyHasher Keccak256::hasher() const
{
    return bcos::crypto::hasher::AnyHasher{hasher::openssl::OpenSSL_Keccak256_Hasher{}};
}

HashType sha256Hash(bytesConstRef _data)
{
    hasher::openssl::OpenSSL_SHA2_256_Hasher hasher;
    hasher.update(_data);

    HashType out;
    hasher.final(out);
    return out;
}

Sha256::Sha256()
{
    setHashImplType(HashImplType::Sha3);
}

HashType Sha256::hash(bytesConstRef _data) const
{
    return sha256Hash(_data);
}

bcos::crypto::hasher::AnyHasher Sha256::hasher() const
{
    return bcos::crypto::hasher::AnyHasher{hasher::openssl::OpenSSL_SHA3_256_Hasher{}};
}

HashType sha3Hash(bytesConstRef _data)
{
    hasher::openssl::OpenSSL_SHA3_256_Hasher hasher;
    hasher.update(_data);

    HashType out;
    hasher.final(out);
    return out;
}

Sha3::Sha3()
{
    setHashImplType(HashImplType::Sha3);
}

HashType Sha3::hash(bytesConstRef _data) const
{
    return sha3Hash(_data);
}

bcos::crypto::hasher::AnyHasher Sha3::hasher() const
{
    return bcos::crypto::hasher::AnyHasher{hasher::openssl::OpenSSL_SHA3_256_Hasher{}};
}

HashType sm3Hash(bytesConstRef _data)
{
    hasher::openssl::OpenSSL_SM3_Hasher hasher;
    hasher.update(_data);

    HashType out;
    hasher.final(out);
    return out;
}

SM3::SM3()
{
    setHashImplType(HashImplType::Sm3Hash);
}

HashType SM3::hash(bytesConstRef _data) const
{
    return sm3Hash(_data);
}

bcos::crypto::hasher::AnyHasher SM3::hasher() const
{
    return bcos::crypto::hasher::AnyHasher{hasher::openssl::OpenSSL_SM3_Hasher{}};
}
}  // namespace bcos::crypto