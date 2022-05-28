/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @brief implementation for fast-sm2 keyPairFactory
 * @file FastSM2KeyPairFactory.h
 * @date 2022.01.17
 * @author yujiechen
 */
#pragma once
#include <bcos-crypto/signature/fastsm2/FastSM2KeyPair.h>
#include <bcos-crypto/signature/fastsm2/FastSM2KeyPairFactory.h>
#include <bcos-crypto/signature/sm2/SM2KeyPairFactory.h>
#include <memory>
namespace bcos
{
namespace crypto
{
class FastSM2KeyPairFactory : public SM2KeyPairFactory
{
public:
    using Ptr = std::shared_ptr<FastSM2KeyPairFactory>;
    FastSM2KeyPairFactory() : SM2KeyPairFactory() {}
    ~FastSM2KeyPairFactory() override {}

    KeyPairInterface::UniquePtr createKeyPair() override
    {
        return std::make_unique<FastSM2KeyPair>();
    }

    KeyPairInterface::UniquePtr createKeyPair(SecretPtr _secretKey) override
    {
        return std::make_unique<FastSM2KeyPair>(_secretKey);
    }
};
}  // namespace crypto
}  // namespace bcos