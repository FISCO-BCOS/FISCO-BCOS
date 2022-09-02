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
 * @brief : Data Encryption
 * @author: chuwen
 * @date: 2018-12-06
 */

#pragma once
#include "Common.h"
#include <bcos-crypto/interfaces/crypto/SymmetricEncryption.h>
#include <bcos-framework/security/DataEncryptInterface.h>
#include <bcos-tool/NodeConfig.h>
#include <bcos-utilities/FileUtility.h>
#include <memory>

namespace bcos
{
namespace security
{
class DataEncryption : public DataEncryptInterface
{
public:
    using Ptr = std::shared_ptr<DataEncryption>;

public:
    DataEncryption(const bcos::tool::NodeConfig::Ptr nodeConfig) : m_nodeConfig(nodeConfig) {}
    ~DataEncryption() override {}

public:
    void init() override;

    std::shared_ptr<bytes> decryptContents(const std::shared_ptr<bytes>& contents) override;

    // use to decrypt node.key
    std::shared_ptr<bytes> decryptFile(const std::string& filename) override;

    // use to encrypt/decrypt in rocksdb
    std::string encrypt(const std::string& data) override;
    std::string decrypt(const std::string& data) override;

private:
    bcos::tool::NodeConfig::Ptr m_nodeConfig{nullptr};

    std::string m_dataKey;
    bcos::crypto::SymmetricEncryption::Ptr m_symmetricEncrypt{nullptr};
};

}  // namespace security

}  // namespace bcos
