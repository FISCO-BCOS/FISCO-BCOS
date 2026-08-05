// evmone: Fast Ethereum Virtual Machine implementation
// Copyright 2025 The evmone Authors.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <bcos-evm/eth/state/blob_params.hpp>

namespace evmone::test
{
using BlobSchedule = std::unordered_map<std::string, state::BlobParams>;

/// Returns the hardcoded blob params for the given EVM revision.
/// After Prague, the blob params must be derived from config.
state::BlobParams get_blob_params(evmc_revision rev);

/// Returns the blob params for the given EVM revision and a blob schedule.
state::BlobParams get_blob_params(evmc_revision rev, const BlobSchedule& blob_schedule);

/// Returns the blob params for given a description of a test network (e.g. transitioning
/// across two forks at some time), a blob schedule and the timestamp.
state::BlobParams get_blob_params(
    std::string_view network, const BlobSchedule& blob_schedule, int64_t timestamp);
}  // namespace evmone::test
