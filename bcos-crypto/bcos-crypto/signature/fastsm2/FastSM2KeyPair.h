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
 * @brief implementation for fast-sm2 keyPair
 * @file FastSM2KeyPair.h
 * @date 2022.01.17
 * @author yujiechen
 */
#pragma once
#include <bcos-crypto/signature/fastsm2/fast_sm2.h>
#include <bcos-crypto/signature/sm2/SM2KeyPair.h>

#ifdef SM2_OPTIMIZE

namespace bcos
{
namespace crypto
{
class FastSM2KeyPair : public SM2KeyPair
{
public:
    using Ptr = std::shared_ptr<FastSM2KeyPair>;
    FastSM2KeyPair() : SM2KeyPair() { m_publicKeyDeriver = fast_sm2_derive_public_key; }
    explicit FastSM2KeyPair(SecretPtr _secretKey) : SM2KeyPair(_secretKey) {}
};
}  // namespace crypto
}  // namespace bcos

#endif