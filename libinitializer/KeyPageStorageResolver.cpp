/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#include "KeyPageStorageResolver.h"
#include "AddressTableModeDetection.h"
#include <bcos-table/src/KeyPageStorageDetector.h>
#include <bcos-tool/Exceptions.h>
#include <boost/throw_exception.hpp>

namespace bcos::initializer
{
namespace
{
[[noreturn]] void throwUnexpectedPositiveConfig(std::size_t value)
{
    BOOST_THROW_EXCEPTION(tool::InvalidConfig() << errinfo_comment(
                              "storage.key_page_size is configured as " + std::to_string(value) +
                              " but no legacy KeyPage metadata was found; set it to 0 for new or "
                              "plain storage, or restore the legacy Page data"));
}
}  // namespace

KeyPageSizeResolution resolveKeyPageSize(
    tool::NodeConfig& config, storage::StorageInterface& storage, ::rocksdb::DB& stateDB)
{
    if (!hasAnyTableRegistration(stateDB))
    {
        if (config.keyPageSizeConfigured() && config.keyPageSize() != 0)
        {
            throwUnexpectedPositiveConfig(config.keyPageSize());
        }
        config.setKeyPageSize(0);
        return {0, "empty", 0};
    }
    if (config.keyPageSizeConfigured() && config.keyPageSize() == 0)
    {
        return {0, "config-skipped", 0};
    }

    std::size_t examined = 0;
    auto hasMetadata = storage::hasKeyPageMetadata(storage, &examined);
    if (hasMetadata)
    {
        if (config.keyPageSizeConfigured())
        {
            return {config.keyPageSize(), "config-verified", examined};
        }
        config.setKeyPageSize(10240);
        return {10240, "detection", examined};
    }
    if (config.keyPageSizeConfigured())
    {
        throwUnexpectedPositiveConfig(config.keyPageSize());
    }

    config.setKeyPageSize(0);
    return {0, "plain", examined};
}
}  // namespace bcos::initializer
