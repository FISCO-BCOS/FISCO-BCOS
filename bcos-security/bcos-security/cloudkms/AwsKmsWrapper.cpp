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
 */
/**
 * @brief : AWS KMS Wrapper
 * @file AwsKmsWrapper.cpp
 * @author: HaoXuan40404
 * @date 2024-11-07
 */
#include "AwsKmsWrapper.h"
#include "Common.h"
#include "bcos-utilities/FileUtility.h"
#include <aws/core/auth/AWSCredentials.h>
#include <aws/core/utils/Array.h>
#include <aws/kms/model/DecryptRequest.h>
#include <aws/kms/model/EncryptRequest.h>
namespace bcos::security
{

AWSKMSWrapper::AWSKMSWrapper(const std::string& region, const std::string& accessKey,
    const std::string& secretKey, const std::string& keyId)
  : m_keyId(keyId)
{
    // create credentials

    AWSKMSWrapper(region, accessKey, secretKey);
}
AWSKMSWrapper::AWSKMSWrapper(
    const std::string& region, const std::string& accessKey, const std::string& secretKey)
{
    // create credentials
    Aws::Auth::AWSCredentials credentials(accessKey.c_str(), secretKey.c_str());

    // configure client
    Aws::Client::ClientConfiguration config;
    config.region = region;

    // use credentials and config to create client
    m_kmsClient = std::make_shared<Aws::KMS::KMSClient>(credentials, config);
}


std::shared_ptr<bytes> AWSKMSWrapper::encryptContents(const std::shared_ptr<bytes>& contents)
{
    Aws::KMS::Model::EncryptRequest request;
    request.SetKeyId(m_keyId);

    Aws::Utils::ByteBuffer plaintextBlob(
        reinterpret_cast<const unsigned char*>(contents->data()), contents->size());
    request.SetPlaintext(plaintextBlob);

    auto outcome = m_kmsClient->Encrypt(request);
    if (!outcome.IsSuccess())
    {
        std::cerr << "Encryption error: " << outcome.GetError().GetMessage() << std::endl;
        return nullptr;
    }

    const auto& resultBlob = outcome.GetResult().GetCiphertextBlob();
    std::shared_ptr<bytes> ciphertext = std::make_shared<bytes>(
        resultBlob.GetUnderlyingData(), resultBlob.GetUnderlyingData() + resultBlob.GetLength());
    return ciphertext;
}

std::shared_ptr<bytes> AWSKMSWrapper::encryptFile(const std::string& inputFilePath)
{
    auto plaintext = readContents(inputFilePath);
    return encryptContents(plaintext);
}

std::shared_ptr<bytes> AWSKMSWrapper::decryptContents(const std::shared_ptr<bytes>& ciphertext)
{
    Aws::KMS::Model::DecryptRequest request;

    Aws::Utils::ByteBuffer ciphertextBlob(ciphertext->data(), ciphertext->size());
    request.SetCiphertextBlob(ciphertextBlob);

    auto outcome = m_kmsClient->Decrypt(request);
    if (!outcome.IsSuccess())
    {
        std::cerr << "Decryption error: " << outcome.GetError().GetMessage() << std::endl;
        return std::make_shared<bytes>();
    }

    const auto& resultBlob = outcome.GetResult().GetPlaintext();
    std::shared_ptr<bytes> plaintext = std::make_shared<bytes>(
        resultBlob.GetUnderlyingData(), resultBlob.GetUnderlyingData() + resultBlob.GetLength());

    return plaintext;
}

std::shared_ptr<bytes> AWSKMSWrapper::decryptFile(const std::string& filename)
{
    std::shared_ptr<bytes> contents = readContents(filename);
    if (contents == nullptr)
    {
        std::cerr << "Failed to read file: " << filename << std::endl;
        return nullptr;
    }
    return decryptContents(contents);
}
}  // namespace bcos::security