/*
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
 * @file HashImpl.h
 * @author: yujiechen
 * @date: 2021-04-06
 */
#pragma once
#include "../../interfaces/crypto/Hash.h"
#include <wedpr-crypto/WedprCrypto.h>
using namespace bcos;
using namespace bcos::crypto;
namespace bcos
{
namespace test
{
class Keccak256Hash : public Hash
{
public:
    using Ptr = std::shared_ptr<Keccak256Hash>;
    Keccak256Hash() { setHashImplType(HashImplType::Keccak256Hash); }
    virtual ~Keccak256Hash() {}
    HashType hash(bytesConstRef _data) override
    {
        HashType hashData;
        CInputBuffer hashInput{(const char*)_data.data(), _data.size()};
        COutputBuffer hashResult{(char*)hashData.data(), HashType::size};
        wedpr_keccak256_hash(&hashInput, &hashResult);
        return hashData;
    }
};
class Sm3Hash : public Hash
{
public:
    using Ptr = std::shared_ptr<Sm3Hash>;
    Sm3Hash() { setHashImplType(HashImplType::Sm3Hash); }
    virtual ~Sm3Hash() {}
    HashType hash(bytesConstRef _data) override
    {
        HashType hashData;
        CInputBuffer hashInput{(const char*)_data.data(), _data.size()};
        COutputBuffer hashResult{(char*)hashData.data(), HashType::size};
        wedpr_sm3_hash(&hashInput, &hashResult);
        return hashData;
    }
};
}  // namespace test
}  // namespace bcos
