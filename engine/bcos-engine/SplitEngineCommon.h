/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <bcos-framework/engine/Types.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

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
std::vector<std::string> supportedCapabilities();
bool isGetPayloadVersionCompatible(ApiVersion requestVersion, std::uint32_t payloadVersion);
std::optional<std::string> validatePayloadAttributes(
    const PayloadAttributes& payloadAttributes, std::uint32_t version);
std::optional<PayloadID> derivePayloadId(
    const PayloadAttributes& payloadAttributes, const h256& parentHash, std::uint32_t version);
PayloadStatus makeStatus(PayloadValidationStatus status,
    std::optional<h256> latestValidHash = std::nullopt,
    std::optional<std::string> validationError = std::nullopt);
}  // namespace split_detail

}  // namespace bcos::engine
