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
 * @brief Common for libinitializer
 * @file Common.cpp
 * @author: yujiechen
 * @date 2021-06-10
 */

#include "Common.h"
#include <bcos-framework/security/KeyEncryptInterface.h>
#include <bcos-tool/Exceptions.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/Exceptions.h>
#include <bcos-utilities/FileUtility.h>
#include <openssl/engine.h>
#include <openssl/rsa.h>
#include <boost/filesystem.hpp>

namespace bcos::initializer
{
bytes loadPrivateKey(std::string const& _keyPath, unsigned _hexedPrivateKeySize,
    std::shared_ptr<bcos::security::KeyEncryptInterface> const& _certEncryptionHandler)
{
    std::shared_ptr<EC_KEY> ecKey;
    try
    {
        auto content = std::make_shared<bytes>(readContents(boost::filesystem::path(_keyPath)));
        auto keyContent = content;
        if (_certEncryptionHandler)
        {
            keyContent = _certEncryptionHandler->decryptContents(content);
        }

        if (keyContent->empty())
        {
            return {};
        }

        INITIALIZER_LOG(INFO) << LOG_BADGE("SecureInitializer") << LOG_DESC("loading privateKey");
        std::shared_ptr<BIO> bioMem(BIO_new(BIO_s_mem()), [](BIO* bio) { BIO_free(bio); });
        BIO_write(bioMem.get(), keyContent->data(), static_cast<int>(keyContent->size()));

        std::shared_ptr<EVP_PKEY> evpPKey(PEM_read_bio_PrivateKey(bioMem.get(), NULL, NULL, NULL),
            [](EVP_PKEY* evpPKey) { EVP_PKEY_free(evpPKey); });
        if (!evpPKey)
        {
            return {};
        }
        ecKey.reset(EVP_PKEY_get1_EC_KEY(evpPKey.get()), [](EC_KEY* ecKey) { EC_KEY_free(ecKey); });
    }
    catch (bcos::Exception& e)
    {
        INITIALIZER_LOG(ERROR) << LOG_BADGE("SecureInitializer")
                               << LOG_DESC("parse privateKey failed") << LOG_KV("file", _keyPath)
                               << LOG_KV("EINFO", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(bcos::tool::InvalidConfig() << errinfo_comment(
                                  "SecureInitializer: parse privateKey failed:" + _keyPath));
    }
    std::shared_ptr<const BIGNUM> ecPrivateKey(
        EC_KEY_get0_private_key(ecKey.get()), [](const BIGNUM*) {});

    std::shared_ptr<char> privateKeyData(
        BN_bn2hex(ecPrivateKey.get()), [](char* privateKey) { OPENSSL_free(privateKey); });
    std::string keyHex(privateKeyData.get());
    if (keyHex.size() < _hexedPrivateKeySize)
    {
        keyHex.insert(0, _hexedPrivateKeySize - keyHex.size(), '0');
    }
    return fromHex(keyHex);
}
}  // namespace bcos::initializer