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
 * @file Signature.cpp
 * @author: lucasli
 * @date 2022-12-14
 */
#include <bcos-cpp-sdk/utilities/Common.h>
#include <bcos-cpp-sdk/utilities/crypto/Signature.h>
#include <bcos-crypto/interfaces/crypto/KeyInterface.h>
#include <bcos-crypto/signature/hsmSM2/HsmSM2Crypto.h>
#include <bcos-crypto/signature/key/KeyPair.h>
#include <bcos-utilities/BoostLog.h>
#include <boost/throw_exception.hpp>
#include <exception>
#include <memory>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::cppsdk::utilities;

std::shared_ptr<bcos::bytes> Signature::sign(const bcos::crypto::KeyPairInterface& _keyPair,
    const bcos::crypto::HashType& _hash, const std::string _hsmLibPath)
{
    auto crypto_type = _keyPair.keyPairType();
    if (crypto_type == CryptoType::HsmSM2)
    {
        auto signatureImpl = std::make_shared<bcos::crypto::HsmSM2Crypto>(_hsmLibPath);
        return signatureImpl->sign(_keyPair, _hash);
    }
    else
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("sign don't unsupport algorithm "));
    }
}

bool Signature::verify(CryptoType _cryptoType, std::shared_ptr<bcos::bytes const> _pubKeyBytes,
    const bcos::crypto::HashType& _hash, bcos::bytesConstRef _signatureData,
    const std::string _hsmLibPath)
{
    if (_cryptoType == CryptoType::HsmSM2)
    {
        auto signatureImpl = std::make_shared<bcos::crypto::HsmSM2Crypto>(_hsmLibPath);
        return signatureImpl->verify(_pubKeyBytes, _hash, _signatureData);
    }
    else
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("sign don't unsupport algorithm "));
    }
}

bcos::crypto::PublicPtr Signature::recover(CryptoType _cryptoType,
    const bcos::crypto::HashType& _hash, bcos::bytesConstRef _signatureData,
    const std::string _hsmLibPath)
{
    if (_cryptoType == CryptoType::HsmSM2)
    {
        auto signatureImpl = std::make_shared<bcos::crypto::HsmSM2Crypto>(_hsmLibPath);
        return signatureImpl->recover(_hash, _signatureData);
    }
    else
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("sign don't unsupport algorithm "));
    }
}

std::pair<bool, bcos::bytes> Signature::recoverAddress(CryptoType _cryptoType,
    bcos::crypto::Hash::Ptr _hashImpl, bcos::bytesConstRef _in, const std::string _hsmLibPath)
{
    if (_cryptoType == CryptoType::HsmSM2)
    {
        auto signatureImpl = std::make_shared<bcos::crypto::HsmSM2Crypto>(_hsmLibPath);
        return signatureImpl->recoverAddress(_hashImpl, _in);
    }
    else
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("sign don't unsupport algorithm "));
    }
}
