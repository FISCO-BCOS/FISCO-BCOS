/**
 *  Copyright (C) 2023 FISCO BCOS.
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
 * @file ContractShardUtils.h
 * @author: JimmyShi22
 * @date 2023-01-03
 */

#pragma once
#include <bcos-table/src/StorageWrapper.h>

namespace bcos::storage
{
#define CONTRACT_SHARD_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("Sharding")

static const std::string_view SHARD_ROOT_PREFIX = "shard:";
static const std::string_view INHERENT_PREFIX = "inherent:";

static const std::string_view ACCOUNT_SHARD = "shard";

class ContractShardUtils
{
public:
    static void setContractShard(bcos::storage::StorageWrapper& storage,
        const std::string_view& contractTableName, const std::string_view& shard);
    static std::string getContractShard(
        bcos::storage::StorageWrapper& storage, const std::string_view& contractTableName);
    static void setContractShardByParent(bcos::storage::StorageWrapper& storage,
        const std::string_view& parentTableName, const std::string_view& contractTableName);

private:
    static std::optional<bcos::storage::Entry> getShard(
        bcos::storage::StorageWrapper& storage, const std::string_view& contractTableName);
    static void setShard(bcos::storage::StorageWrapper& storage,
        const std::string_view& contractTableName, const std::string_view& shard);

    static bool isInherent(std::optional<bcos::storage::Entry> entry)
    {
        return !entry ? false : entry->getField(0).starts_with(INHERENT_PREFIX);
    }
};
}  // namespace bcos::storage
