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
 * @brief implementation for SM2KeyPair
 * @file SM2KeyPair.cpp
 * @date 2021.03.10
 * @author yujiechen
 */
#include "SM2KeyPair.h"
#include "../../hash/SM3.h"
#include "../Exceptions.h"

using namespace bcos;
using namespace bcos::crypto;
PublicPtr bcos::crypto::sm2PriToPub(SecretPtr _secretKey)
{
    CInputBuffer privateKey{_secretKey->constData(), _secretKey->size()};
    auto pubKey = std::make_shared<KeyImpl>(SM2_PUBLIC_KEY_LEN);
    COutputBuffer publicKey{pubKey->mutableData(), pubKey->size()};
    auto retCode = wedpr_sm2_derive_public_key(&privateKey, &publicKey);
    if (retCode != WEDPR_SUCCESS)
    {
        BOOST_THROW_EXCEPTION(
            PriToPublicKeyException() << errinfo_comment("sm2PriToPub exception"));
    }
    return pubKey;
}

SM2KeyPair::SM2KeyPair(SecretPtr _secretKey) : SM2KeyPair()
{
    if (_secretKey->size() != SM2_PRIVATE_KEY_LEN)
    {
        BOOST_THROW_EXCEPTION(GenerateKeyPairException() << errinfo_comment(
                                  "Invalid InvalidSecretKey, sm2 secret length must be " +
                                  std::to_string(SM2_PRIVATE_KEY_LEN)));
    }
    m_secretKey = _secretKey;
    m_publicKey = priToPub(_secretKey);
}