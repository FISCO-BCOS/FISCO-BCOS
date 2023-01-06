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
 * @file ContractShardUtils.cpp
 * @author: JimmyShi22
 * @date 2023-01-03
 */


#include "ContractShardUtils.h"
#include "../../Common.h"
#include "Common.h"
using namespace bcos::precompiled;
using namespace bcos::storage;
using namespace bcos::executor;

void ContractShardUtils::setContractShard(bcos::storage::StorageWrapper& storage,
    const std::string_view& contractTableName, const std::string_view& shard)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("ContractShard") << "setContractShard "
                           << LOG_KV("contractTableName", contractTableName)
                           << LOG_KV("shard", shard);

    setShard(storage, contractTableName, std::string(SHARD_ROOT_PREFIX) + std::string(shard));
}

std::optional<bcos::storage::Entry> ContractShardUtils::getShard(
    bcos::storage::StorageWrapper& storage, const std::string_view& contractTableName)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("ContractShard") << "getContractShard "
                           << LOG_KV("contractTableName", contractTableName);

    auto contractTable = storage.openTable(contractTableName);

    if (!contractTable)
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("ContractShard")
                                 << "getContractShard openTable failed"
                                 << LOG_KV("contractTableName", contractTableName);
        return {};
    }


    return storage.getRow(contractTableName, ACCOUNT_SHARD);
}

void ContractShardUtils::setShard(bcos::storage::StorageWrapper& storage,
    const std::string_view& contractTableName, const std::string_view& shard)
{
    auto contractTable = storage.openTable(contractTableName);

    if (!contractTable)
    {
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("ContractShard") << "setShard openTable failed"
                                 << LOG_KV("contractTableName", contractTableName)
                                 << LOG_KV("shard", shard);
        BOOST_THROW_EXCEPTION(
            BCOS_ERROR(CODE_INVALID_OPENTABLE_FAILED, "setShard openTable failed"));
        return;
    }

    Entry shardEntry;
    shardEntry.importFields({std::string(shard)});
    storage.setRow(contractTableName, ACCOUNT_SHARD, std::move(shardEntry));
    return;
}

std::string ContractShardUtils::getContractShard(
    bcos::storage::StorageWrapper& storage, const std::string_view& contractTableName)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("ContractShard") << "getContractShard "
                           << LOG_KV("contractTableName", contractTableName);

    std::string_view tableName = contractTableName;
    std::optional<bcos::storage::Entry> entry;
    do
    {
        if (entry)
        {
            tableName = entry->getField(0);
            tableName.remove_prefix(INHERENT_PREFIX.length());
        }
        entry = getShard(storage, tableName);

    } while (isInherent(entry));

    if (entry)
    {
        auto shardName = entry->getField(0);
        shardName.remove_prefix(SHARD_ROOT_PREFIX.length());
        return std::string(shardName);
    }

    return {};
}

void ContractShardUtils::setContractShardByParent(bcos::storage::StorageWrapper& storage,
    const std::string_view& parentTableName, const std::string_view& contractTableName)
{
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("ContractShard") << "setContractShardByParent "
                           << LOG_KV("contractTableName", contractTableName)
                           << LOG_KV("parentTableName", parentTableName);


    if (contractTableName.compare(parentTableName) == 0)
    {
        PRECOMPILED_LOG(WARNING)
            << LOG_BADGE("ContractShard")
            << "setContractShardByParent failed, could not set shard parent as itself"
            << LOG_KV("contractTableName", contractTableName)
            << LOG_KV("parentTableName", parentTableName);
        return;
    }

    setShard(
        storage, contractTableName, std::string(INHERENT_PREFIX) + std::string(parentTableName));
}