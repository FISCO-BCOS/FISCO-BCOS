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
 * @brief Common for libinitializer
 * @file Common.h
 * @author: yujiechen
 * @date 2021-06-10
 */
#pragma once
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/Common.h>
#include <memory>

#define INITIALIZER_LOG(LEVEL) BCOS_LOG(LEVEL) << "[INITIALIZER]"
namespace bcos::security
{
class KeyEncryptInterface;
}

namespace bcos::initializer
{
bcos::bytes loadPrivateKey(std::string const& _keyPath, unsigned _hexedPrivateKeySize,
    std::shared_ptr<bcos::security::KeyEncryptInterface> const& _certEncryptionHandler);
}  // namespace bcos::initializer
