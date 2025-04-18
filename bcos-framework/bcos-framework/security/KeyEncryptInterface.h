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
 */
/**
 * @brief : Encrypt key file
 * @author: HaoXuan40404
 * @date: 2024-11-07
 */

#pragma once
#include <bcos-utilities/Common.h>
#include <memory>

namespace bcos::security
{
class KeyEncryptInterface
{
public:
    using Ptr = std::shared_ptr<KeyEncryptInterface>;
    KeyEncryptInterface() = default;

    virtual ~KeyEncryptInterface() = default;

    // use to encrypt node.key
    virtual std::shared_ptr<bytes> encryptContents(const std::shared_ptr<bytes>& contents) = 0;
    virtual std::shared_ptr<bytes> encryptFile(const std::string& filename) = 0;

    // use to decrypt node.key
    virtual std::shared_ptr<bytes> decryptContents(const std::shared_ptr<bytes>& contents) = 0;
    virtual std::shared_ptr<bytes> decryptFile(const std::string& filename) = 0;
};

}  // namespace bcos::security