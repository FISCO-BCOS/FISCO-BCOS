/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file BlobSchedule.h
 * @brief EIP-7840 blob schedule table (target/max blobs per block + the blob
 *        base-fee update fraction), the single source of truth shared by the
 *        devp2p header validator (excess-blob-gas recomputation) and the v2
 *        executor (blob gas price / per-block blob gas limit). Lives in
 *        bcos-framework (like TxGasModel.h) because the ethereum-executor
 *        deliberately does not depend on bcos-evm (see EVMSupport.h).
 */
#pragma once

#include <cstdint>
#include <limits>

namespace bcos::protocol
{
// EIP-4844: blob gas carried by one blob (2**17).
inline constexpr uint64_t BLOB_GAS_PER_BLOB = 131072;

// EIP-7840 blob schedule entry. Values match geth's params/config.go:
// DefaultCancunBlobConfig 3/6/3338477, DefaultPragueBlobConfig 6/9/5007716,
// DefaultBPO1BlobConfig 10/15/8346193, DefaultBPO2BlobConfig 14/21/11684671.
// Sanity rule (geth, post-Prague): (max - target) * 131072 / fraction == 0.078522,
// the EIP-7691 "8.2% max increase per block" constant.
struct BlobScheduleConfig
{
    uint64_t targetBlobs = 0;
    uint64_t maxBlobs = 0;
    uint64_t baseFeeUpdateFraction = 0;
};

inline constexpr BlobScheduleConfig CANCUN_BLOB_SCHEDULE{3, 6, 3338477};
inline constexpr BlobScheduleConfig PRAGUE_BLOB_SCHEDULE{6, 9, 5007716};
// Osaka keeps the Prague schedule — EIP-7918 changes only the excess-blob-gas
// UPDATE rule, not target/max.
inline constexpr BlobScheduleConfig OSAKA_BLOB_SCHEDULE = PRAGUE_BLOB_SCHEDULE;
inline constexpr BlobScheduleConfig BPO1_BLOB_SCHEDULE{10, 15, 8346193};
inline constexpr BlobScheduleConfig BPO2_BLOB_SCHEDULE{14, 21, 11684671};

// Timestamp-keyed fork points driving the blob schedule. 0 = active from genesis;
// UINT64_MAX (the default) = "not scheduled", matching NodeConfig's
// readOptionalTs semantics for absent tail keys. Osaka is not listed: it keeps
// the Prague params (EIP-7918's update rule is the consumers' concern).
struct BlobForkTimes
{
    uint64_t cancunTime{std::numeric_limits<uint64_t>::max()};
    uint64_t pragueTime{std::numeric_limits<uint64_t>::max()};
    uint64_t bpo1Time{std::numeric_limits<uint64_t>::max()};
    uint64_t bpo2Time{std::numeric_limits<uint64_t>::max()};
};

inline constexpr bool blobForkActive(uint64_t forkTime, uint64_t timestamp)
{
    return forkTime == 0 || timestamp >= forkTime;
}

// EIP-7840: the blob schedule in effect for the block at `timestamp` (seconds).
// Returns the zero entry pre-Cancun (no blob fields exist there); mirrors the
// fork-priority order of geth's latestBlobConfig.
inline constexpr BlobScheduleConfig blobScheduleForTimestamp(
    BlobForkTimes const& forks, uint64_t timestamp)
{
    if (blobForkActive(forks.bpo2Time, timestamp))
    {
        return BPO2_BLOB_SCHEDULE;
    }
    if (blobForkActive(forks.bpo1Time, timestamp))
    {
        return BPO1_BLOB_SCHEDULE;
    }
    if (blobForkActive(forks.pragueTime, timestamp))
    {
        return PRAGUE_BLOB_SCHEDULE;
    }
    if (blobForkActive(forks.cancunTime, timestamp))
    {
        return CANCUN_BLOB_SCHEDULE;
    }
    return {};
}
}  // namespace bcos::protocol
