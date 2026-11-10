/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <bcos-framework/storage/StorageInterface.h>
#include <bcos-tool/NodeConfig.h>
#include <cstddef>
#include <string>

namespace rocksdb
{
class DB;
}

namespace bcos::initializer
{
struct KeyPageSizeResolution
{
    size_t value;
    std::string source;
    size_t tablesExamined;
};

KeyPageSizeResolution resolveKeyPageSize(
    tool::NodeConfig& config, storage::StorageInterface& storage, ::rocksdb::DB& stateDB);
}  // namespace bcos::initializer
