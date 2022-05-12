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
#include "Common.h"
#include <bcos-crypto/interfaces/crypto/SymmetricEncryption.h>
#include <memory>

namespace bcos
{

namespace security
{

class EncryptedFile
{
public:
    using Ptr = std::shared_ptr<EncryptedFile>;

public:
    EncryptedFile(bool isSm);
    ~EncryptedFile() = default;

public:
    std::shared_ptr<bytes> decryptContents(
        const std::shared_ptr<bytes>& contents, const std::string& dataKey);

private:
    bcos::crypto::SymmetricEncryption::Ptr m_symmetricEncrypt{nullptr};
};

}  // namespace security

}  // namespace bcos