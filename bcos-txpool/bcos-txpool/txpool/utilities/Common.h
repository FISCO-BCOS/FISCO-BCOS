/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file Common.h
 * @author: kyonGuo
 * @date 2023/2/9
 */

#pragma once
#include "bcos-utilities/Common.h"

namespace bcos::txpool
{
// Trigger a transaction cleanup operation every 3s
static constexpr const uint64_t TXPOOL_CLEANUP_TIME = 3000;
// the txs expiration time, default is 10 minutes
static constexpr const uint64_t TX_DEFAULT_EXPIRATION_TIME = uint64_t(60 * 10 * 1000);
// Maximum number of transactions traversed by m_cleanUpTimer,
// The limit set here is to minimize the impact of the cleanup operation on txpool performance
static constexpr const uint64_t MAX_TRAVERSE_TXS_COUNT = 10000;
static constexpr const size_t MAX_RETRY_NOTIFY_TIME = 3;
static constexpr const size_t DEFAULT_POOL_LIMIT = 15000;
static constexpr const int64_t DEFAULT_BLOCK_LIMIT = 600;
}  // namespace bcos::txpool
