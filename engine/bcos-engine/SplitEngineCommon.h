/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <bcos-framework/engine/Types.h>

#include <memory>
#include <optional>

namespace bcos::engine
{

struct CommonPayloadEntry
{
    std::uint32_t version = 0;
    ExecutionPayload executionPayload;
    u256 blockValue = 0;
    std::optional<BlobsBundleV1> blobsBundle;
    bool shouldOverrideBuilder = false;
    std::optional<h256> parentBeaconBlockRoot;
};

using CommonPayloadEntryPtr = std::shared_ptr<const CommonPayloadEntry>;

namespace split_detail
{
bool isGetPayloadVersionCompatible(ApiVersion requestVersion, std::uint32_t payloadVersion);
}  // namespace split_detail

}  // namespace bcos::engine
