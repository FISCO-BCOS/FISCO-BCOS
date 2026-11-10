/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <bcos-framework/storage/StorageInterface.h>
#include <cstddef>

namespace bcos::storage
{
bool hasKeyPageMetadata(StorageInterface& storage, std::size_t* examinedTables = nullptr);
}
