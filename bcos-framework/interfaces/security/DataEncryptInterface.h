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
 * @brief : Encrypt file
 * @author: jimmyshi, websterchen
 * @date: 2018-12-06
 */

#pragma once

namespace bcos
{

namespace security
{

class DataEncryptInterface
{
public:
    using Ptr = std::shared_ptr<DataEncryptInterface>;

    DataEncryptInterface() = default;
    ~DataEncryptInterface() = default;

public:
    virtual void init() = 0;

    // use to decrypt node.key
    virtual std::shared_ptr<bytes> decryptContents(const std::shared_ptr<bytes>& contents) = 0;
    virtual std::shared_ptr<bytes> decryptFile(const std::string& filename) = 0;

    // use to encrypt/decrypt in rocksdb
    virtual std::string encrypt(const std::string& data) = 0;
    virtual std::string decrypt(const std::string& data) = 0;
};

}  // namespace security

}  // namespace bcos