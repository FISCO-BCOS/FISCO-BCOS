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
 * @brief common types for crypto
 * @file CommonType.h
 * @author: yujiechen
 * @date 2021-04-01
 */
#pragma once
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/FixedBytes.h>

#define CRYPTO_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("CRYPTO")
namespace bcos::crypto
{
using HashType = h256;
using HashList = std::vector<HashType>;
using HashListPtr = std::shared_ptr<HashList>;

}  // namespace bcos::crypto