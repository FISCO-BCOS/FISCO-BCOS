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
 * @brief Hash algorithm of sha3
 * @file Sha3.h
 * @date 2021.04.01
 * @author yujiechen
 */
#pragma once
#include <bcos-crypto/interfaces/crypto/Hash.h>
#include <wedpr-crypto/WedprCrypto.h>
#include <wedpr-crypto/WedprUtilities.h>

namespace bcos
{
namespace crypto
{
HashType inline sha3Hash(bytesConstRef _data)
{
    hasher::openssl::OpenSSL_SHA3_256_Hasher hasher;
    hasher.update(_data);

    HashType out;
    hasher.final(out);
    return out;
}
class Sha3 : public Hash
{
public:
    using Ptr = std::shared_ptr<Sha3>;
    Sha3() { setHashImplType(HashImplType::Sha3); }
    virtual ~Sha3() {}
    HashType hash(bytesConstRef _data) override { return sha3Hash(_data); }
    bcos::crypto::hasher::AnyHasher hasher() override
    {
        return bcos::crypto::hasher::AnyHasher{
            bcos::crypto::hasher::openssl::OpenSSL_SHA3_256_Hasher{}};
    };
};
}  // namespace crypto
}  // namespace bcos