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
 * @brief implementation for KeyFactory
 * @file KeyFactoryImpl.h
 * @date 2021.05.10
 * @author yujiechen
 */
#pragma once
#include "KeyImpl.h"
#include <bcos-framework/interfaces/crypto/KeyFactory.h>

namespace bcos
{
namespace crypto
{
class KeyFactoryImpl : public KeyFactory
{
public:
    using Ptr = std::shared_ptr<KeyFactoryImpl>;
    KeyFactoryImpl() = default;
    ~KeyFactoryImpl() override {}

    KeyInterface::Ptr createKey(bytesConstRef _keyData) override
    {
        return std::make_shared<KeyImpl>(_keyData);
    }

    KeyInterface::Ptr createKey(bytes const& _keyData) override
    {
        return std::make_shared<KeyImpl>(_keyData);
    }
};
}  // namespace crypto
}  // namespace bcos