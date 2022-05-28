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
 * @file TxPoolTypeDef.h
 * @author: yujiechen
 * @date: 2021-05-07
 */
#pragma once
#include <bcos-crypto/interfaces/crypto/KeyInterface.h>
#include "../Common.h"

#define TXPOOL_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("TXPOOL")

namespace bcos
{
namespace txpool
{
using TxsHashSet = std::set<bcos::crypto::HashType>;
using TxsHashSetPtr = std::shared_ptr<TxsHashSet>;
}  // namespace txpool
}  // namespace bcos