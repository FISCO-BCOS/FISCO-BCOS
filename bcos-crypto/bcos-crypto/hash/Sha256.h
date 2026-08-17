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

namespace bcos::crypto
{
HashType sha256Hash(bytesConstRef _data);

class Sha256 : public Hash
{
public:
    using Ptr = std::shared_ptr<Sha256>;
    Sha256();
    ~Sha256() override = default;
    HashType hash(bytesConstRef _data) const override;
    bcos::crypto::hasher::AnyHasher hasher() const override;
};
}  // namespace bcos::crypto