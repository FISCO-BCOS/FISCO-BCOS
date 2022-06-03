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
 * @file FastSM2Crypto.h
 * @date 2022.01.17
 * @author yujiechen
 */

#pragma once
#include <bcos-crypto/signature/fastsm2/FastSM2KeyPairFactory.h>
#include <bcos-crypto/signature/fastsm2/fast_sm2.h>
#include <bcos-crypto/signature/sm2/SM2Crypto.h>
#include <memory>

#ifdef SM2_OPTIMIZE

namespace bcos
{
namespace crypto
{
class FastSM2Crypto : public SM2Crypto
{
public:
    using Ptr = std::shared_ptr<FastSM2Crypto>;
    FastSM2Crypto() : SM2Crypto()
    {
        m_signer = fast_sm2_sign;
        m_verifier = fast_sm2_verify;
        m_keyPairFactory = std::make_shared<FastSM2KeyPairFactory>();
    }
    virtual ~FastSM2Crypto() {}
};
}  // namespace crypto
}  // namespace bcos

#endif