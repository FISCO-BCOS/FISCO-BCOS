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
 * @brief : KMS Interface
 * @file CloudKmsInterface.h
 * @author: HaoXuan40404
 * @date 2024-11-07
 */
#pragma once
#include <bcos-framework/security/CloudKmsType.h>
#include <bcos-framework/security/KeyEncryptInterface.h>
#include <string>

using namespace std;
// only for nodes, client will shutdown after decrypt
namespace bcos::security
{
class CloudKmsInterface : public KeyEncryptInterface
{
public:
    using Ptr = std::shared_ptr<CloudKmsInterface>;
    // KmsInterface()= default;
    CloudKmsInterface(const bcos::tool::NodeConfig::Ptr nodeConfig);

    std::shared_ptr<bytes> encryptContents(const std::shared_ptr<bytes>& contents) override;
    std::shared_ptr<bytes> encryptFile(const std::string& filename) override;

    std::shared_ptr<bytes> decryptContents(const std::shared_ptr<bytes>& contents) override;
    std::shared_ptr<bytes> decryptFile(const std::string& filename) override;

protected:
    CloudKmsType m_kmsType;
    std::string m_kmsUrl;
};
}  // namespace bcos::security