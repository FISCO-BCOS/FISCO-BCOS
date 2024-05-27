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
 * @brief implementation for hsm SM2 KeyPair
 * @file HsmSM2KeyPair.cpp
 * @date 2022.11.04
 * @author lucasli
 */
#include "hsm-crypto/hsm/CryptoProvider.h"
#include "hsm-crypto/hsm/SDFCryptoProvider.h"
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/signature/Exceptions.h>
#include <bcos-crypto/signature/hsmSM2/HsmSM2KeyPair.h>

using namespace bcos;
using namespace bcos::crypto;
using namespace hsm;

#define SDR_OK 0x0

HsmSM2KeyPair::HsmSM2KeyPair(std::string _libPath, SecretPtr _secretKey) : HsmSM2KeyPair(_libPath)
{
    if (_secretKey->size() != HSM_SM2_PRIVATE_KEY_LEN)
    {
        BOOST_THROW_EXCEPTION(GenerateKeyPairException() << errinfo_comment(
                                  "Invalid InvalidSecretKey, hsm sm2 secret length must be " +
                                  std::to_string(HSM_SM2_PRIVATE_KEY_LEN)));
    }
    m_secretKey = _secretKey;
    m_publicKey = priToPub(_secretKey);
}

HsmSM2KeyPair::HsmSM2KeyPair(std::string _libPath, unsigned int _keyIndex, std::string _password)
  : HsmSM2KeyPair(_libPath)
{
    m_keyIndex = _keyIndex;
    m_password = _password;
    m_isInternalKey = true;

    SDFCryptoProvider& provider = SDFCryptoProvider::GetInstance(m_hsmLibPath);

    Key key = Key(m_keyIndex);
    unsigned int code = provider.ExportInternalPublicKey(key, AlgorithmType::SM2);
    if (code != SDR_OK)
    {
        BOOST_THROW_EXCEPTION(GenerateKeyPairException()
                              << errinfo_comment("HsmSM2ExportInternalPublicKey exception"));
    }
    m_publicKey = std::make_shared<KeyImpl>(HSM_SM2_PUBLIC_KEY_LEN, key.publicKey());
}

PublicPtr HsmSM2KeyPair::priToPub(SecretPtr _secretKey)
{
    CInputBuffer privateKey{_secretKey->constData(), _secretKey->size()};
    auto pubKey = std::make_shared<KeyImpl>(HSM_SM2_PUBLIC_KEY_LEN);
    COutputBuffer publicKey{pubKey->mutableData(), pubKey->size()};
    auto retCode = m_publicKeyDeriver(&privateKey, &publicKey);
    if (retCode != WEDPR_SUCCESS)
    {
        BOOST_THROW_EXCEPTION(
            PriToPublicKeyException() << errinfo_comment("HsmSM2PriToPub exception"));
    }
    return pubKey;
}