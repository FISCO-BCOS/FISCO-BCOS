/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "KeyPageStorageResolver.h"
#include "AddressTableModeDetection.h"
#include <bcos-table/src/KeyPageStorageDetector.h>

namespace bcos::initializer
{
KeyPageSizeResolution resolveKeyPageSize(
    tool::NodeConfig& config, storage::StorageInterface& storage, ::rocksdb::DB& stateDB)
{
    if (!hasAnyTableRegistration(stateDB))
    {
        config.setKeyPageSize(0);
        return {0, "empty", 0};
    }
    if (config.keyPageSizeConfigured() && config.keyPageSize() == 0)
    {
        return {0, "config", 0};
    }

    std::size_t examined = 0;
    auto hasMetadata = storage::hasKeyPageMetadata(storage, &examined);
    if (hasMetadata)
    {
        if (config.keyPageSizeConfigured())
        {
            return {config.keyPageSize(), "config", examined};
        }
        config.setKeyPageSize(10240);
        return {10240, "detection", examined};
    }

    config.setKeyPageSize(0);
    return {0, "plain", examined};
}
}  // namespace bcos::initializer
