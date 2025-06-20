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
 * @file KmsInterface.cpp
 * @author: HaoXuan40404
 * @date 2024-11-07
 */
#include "CloudKmsKeyEncryption.h"
#include "../Common.h"
#include "AwsKmsWrapper.h"
#include "bcos-framework/security/CloudKmsType.h"
#include "bcos-utilities/FileUtility.h"
#include "utils.h"
#include <aws/core/Aws.h>
#include <bcos-utilities/Log.h>

namespace bcos::security
{
CloudKmsKeyEncryption::CloudKmsKeyEncryption(const bcos::tool::NodeConfig::Ptr nodeConfig)
  : m_kmsType(nodeConfig->cloudKmsType()), m_kmsUrl(nodeConfig->keyEncryptionUrl())
{}

std::shared_ptr<bytes> CloudKmsKeyEncryption::decryptContents(
    const std::shared_ptr<bytes>& contents)
{
    BCOS_LOG(INFO) << LOG_BADGE("KmsInterface::decryptContents")
                   << LOG_KV("decrypt type", std::string(magic_enum::enum_name(m_kmsType)));
    // m_kmsUrl cotaims secret key

    auto provider = m_kmsType;
    BCOS_LOG(DEBUG) << LOG_BADGE("KmsInterface::decrypt") << LOG_KV("decrypt url", m_kmsUrl);

    if (provider == CloudKmsType::AWS)
    {
        // initialize the AWS SDK
        std::vector<std::string> awsKmsUrlParts;
        if (!splitKmsUrl(m_kmsUrl, awsKmsUrlParts))
        {
            BCOS_LOG(ERROR) << LOG_BADGE("KmsInterface::decrypt")
                            << LOG_KV("Invalid KMS url:", m_kmsUrl);
            BOOST_THROW_EXCEPTION(KmsTypeError());
        }
        std::string region = awsKmsUrlParts[0];
        std::string accessKey = awsKmsUrlParts[1];
        std::string secretKey = awsKmsUrlParts[2];

        try
        {
            // Do not comment here, otherwise the AWS client will be instantiated repeatedly
            static struct AwsSdkLifecycleManager
            {
                Aws::SDKOptions options;
                AwsSdkLifecycleManager()
                {
                    options.cryptoOptions.initAndCleanupOpenSSL = false;
                    Aws::InitAPI(options);
                    BCOS_LOG(INFO) << LOG_BADGE("AwsSdkLifecycleManager") << "AWS SDK Initialized.";
                }
                ~AwsSdkLifecycleManager()
                {
                    Aws::ShutdownAPI(options);
                    BCOS_LOG(INFO) << LOG_BADGE("AwsSdkLifecycleManager") << "AWS SDK Shutdown.";
                }
                AwsSdkLifecycleManager(const AwsSdkLifecycleManager&) = delete;
                AwsSdkLifecycleManager& operator=(const AwsSdkLifecycleManager&) = delete;
                AwsSdkLifecycleManager(AwsSdkLifecycleManager&&) = delete;
                AwsSdkLifecycleManager& operator=(AwsSdkLifecycleManager&&) = delete;
            } sdkLifecycleManager;
            // create an AWS KMS wrapper
            AwsKmsWrapper kmsWrapper(region, accessKey, secretKey);
            std::shared_ptr<bytes> decryptResult = kmsWrapper.decryptContents(contents);
            if (decryptResult == nullptr)
            {
                BCOS_LOG(ERROR) << LOG_BADGE("KmsInterface::decrypt")
                                << LOG_DESC("Decryption failed!");
                BOOST_THROW_EXCEPTION(DecryptFailed());
            }
            BCOS_LOG(INFO) << LOG_BADGE("KmsInterface::decrypt")
                           << LOG_KV("decrypt result size", decryptResult->size());
            BCOS_LOG(INFO) << LOG_BADGE("KmsInterface::decrypt success, shutdown AWS SDK success")
                           << LOG_KV("decrypt size", decryptResult->size());
            return decryptResult;
        }
        catch (const std::exception& e)
        {
            BCOS_LOG(ERROR) << LOG_BADGE("KmsInterface::decrypt") << LOG_KV("Error:", e.what());
            BOOST_THROW_EXCEPTION(DecryptFailed());
        }
    }

    BCOS_LOG(ERROR) << LOG_BADGE("KmsInterface::decrypt")
                    << LOG_KV("Unsupported KMS provider:", std::string(magic_enum::enum_name(m_kmsType)));
    BOOST_THROW_EXCEPTION(KmsTypeError());
}

std::shared_ptr<bytes> CloudKmsKeyEncryption::decryptFile(const std::string& filename)
{
    std::shared_ptr<bytes> contents = readContents(filename);
    if (contents == nullptr)
    {
        BCOS_LOG(ERROR) << LOG_BADGE("KmsInterface::decryptFile")
                        << LOG_KV("Failed to read file:", filename);
        BOOST_THROW_EXCEPTION(DecryptFailed());
    }
    return decryptContents(contents);
}

std::shared_ptr<bytes> CloudKmsKeyEncryption::encryptContents(
    const std::shared_ptr<bytes>& contents)
{
    // Not support
    BCOS_LOG(ERROR) << LOG_BADGE("KmsInterface::encryptContents failed");
    BOOST_THROW_EXCEPTION(EncryptFailed());
}

std::shared_ptr<bytes> CloudKmsKeyEncryption::encryptFile(const std::string& filename)
{
    // Not support
    BCOS_LOG(ERROR) << LOG_BADGE("KmsInterface::encryptFile failed");
    BOOST_THROW_EXCEPTION(EncryptFailed());
}

}  // namespace bcos::security
