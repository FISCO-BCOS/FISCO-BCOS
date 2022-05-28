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
 * @brief implementation for secp256k1 KeyPair
 * @file Secp256k1KeyPair.cpp
 * @date 2021.03.05
 * @author yujiechen
 */
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/signature/Exceptions.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1KeyPair.h>

bcos::crypto::PublicPtr bcos::crypto::secp256k1PriToPub(bcos::crypto::SecretPtr _secret)
{
    CInputBuffer privateKey{_secret->constData(), _secret->size()};

    PublicPtr pubKey = std::make_shared<KeyImpl>(SECP256K1_PUBLIC_LEN);
    COutputBuffer publicKey{pubKey->mutableData(), pubKey->size()};
    auto retCode = wedpr_secp256k1_derive_public_key(&privateKey, &publicKey);
    if (retCode != WEDPR_SUCCESS)
    {
        BOOST_THROW_EXCEPTION(
            PriToPublicKeyException() << errinfo_comment("secp256k1PriToPub exception"));
    }
    return pubKey;
}