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
 *
 * @file KZGPrecompiled.h
 * @author: wenlinli
 * @date: 2024-04-29
 */

#pragma once
#include <bcos-crypto/hash/Sha256.h>
#include <bcos-framework/protocol/Protocol.h>
#include <evmc/evmc.h>

namespace bcos::executor
{

inline constexpr uint8_t kBlobCommitmentVersionKzg{1};

namespace crypto
{

class kzgPrecompiled
{
public:
    using Ptr = std::shared_ptr<kzgPrecompiled>;
    using ConstPtr = std::shared_ptr<const kzgPrecompiled>;
    kzgPrecompiled() = default;
    ~kzgPrecompiled() = default;

    bcos::crypto::HashType kzg2VersionedHash(bytesConstRef input);

    bool verifyKZGProof(
        bytesConstRef commitment, bytesConstRef z, bytesConstRef y, bytesConstRef proof);
};

}  // namespace crypto
}  // namespace bcos::executor
