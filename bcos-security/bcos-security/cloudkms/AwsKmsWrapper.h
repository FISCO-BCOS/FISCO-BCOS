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
 * @file AwsKmsWrapper.h
 * @author: HaoXuan40404
 * @date 2024-11-07
 */
#pragma once
#include <aws/kms/KMSClient.h>
#include <bcos-framework/security/KeyEncryptInterface.h>
#include <string>

namespace bcos::security
{
class AwsKmsWrapper : public KeyEncryptInterface
{
public:
    explicit AwsKmsWrapper(
        const std::string& region, const std::string& accessKey, const std::string& secretKey);
    explicit AwsKmsWrapper(
        const std::string& region, const std::string& accessKey, const std::string& secretKey, const std::string& keyId);
    ~AwsKmsWrapper() = default;

    std::shared_ptr<bytes> encryptContents(const std::shared_ptr<bytes>& contents) override;
    std::shared_ptr<bytes> encryptFile(const std::string& filename) override;

    std::shared_ptr<bytes> decryptContents(const std::shared_ptr<bytes>& contents) override;
    std::shared_ptr<bytes> decryptFile(const std::string& filename) override;

    void setKmsClient(const std::shared_ptr<Aws::KMS::KMSClient>& kmsClient) { m_kmsClient = kmsClient; }

private:
    std::shared_ptr<Aws::KMS::KMSClient> m_kmsClient;
    std::string m_keyId{};
};
}  // namespace bcos::security