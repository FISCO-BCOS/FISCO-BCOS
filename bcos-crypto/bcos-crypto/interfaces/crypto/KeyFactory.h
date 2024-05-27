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
 * @brief interface for Public/Secret key creation
 * @file KeyFactory.h
 * @author: yujiechen
 * @date 2021-05-10
 */
#pragma once
#include <bcos-crypto/interfaces/crypto/KeyInterface.h>
namespace bcos::crypto
{
class KeyFactory
{
public:
    using Ptr = std::shared_ptr<KeyFactory>;
    KeyFactory() = default;
    virtual ~KeyFactory() = default;

    virtual std::shared_ptr<KeyInterface> createKey(bcos::bytesConstRef _keyData) = 0;
    virtual std::shared_ptr<KeyInterface> createKey(bcos::bytes const& _keyData) = 0;
};
}  // namespace bcos::crypto