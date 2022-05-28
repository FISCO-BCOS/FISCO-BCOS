/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/**
 * @brief : Data Encryption
 * @author: chuwen
 * @date: 2018-12-06
 */

#pragma once
#include "Common.h"
#include <bcos-crypto/interfaces/crypto/SymmetricEncryption.h>
#include <bcos-framework//security/DataEncryptInterface.h>
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
    ~DataEncryption() = default;

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
