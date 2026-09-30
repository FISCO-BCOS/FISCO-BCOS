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
 * @file HeaderValidator.h
 * @brief Ethereum PoS header validation (EIP-1559 base fee, EIP-4844 blob gas,
 *        gas-limit bounds, difficulty/nonce/uncle/extra-data rules, fork-gated
 *        field presence for Shanghai/Cancun).
 * @date 2026/8/18
 *
 * The implementation lives in bcos-rlp-protocol/EthPoSHeaderValidation.h
 * (namespace bcos::protocol) so the Engine API newPayload lane
 * (scheduler_v1::EthereumBlockVerifier) validates headers with THE SAME rules —
 * this header only re-exports those names under their historical
 * bcos::devp2p::sync spelling. The devp2p lane's behavior is unchanged.
 */
#pragma once

#include "Block.h"
#include <bcos-rlp-protocol/EthPoSHeaderValidation.h>

namespace bcos::devp2p::sync
{
using protocol::kBlobBaseCost;
using protocol::kElasticityMultiplier;
using protocol::kBaseFeeMaxChangeDenominator;
using protocol::kGasLimitBoundDivisor;
using protocol::kGasPerBlob;
using protocol::kInitialBaseFee;
using protocol::kMaxExtraDataSize;
using protocol::kMinGasLimit;

using BlobSchedule = protocol::BlobGasSchedule;
using protocol::toGasSchedule;
using protocol::kBpo1BlobSchedule;
using protocol::kBpo2BlobSchedule;
using protocol::kCancunBlobSchedule;
using protocol::kOsakaBlobSchedule;
using protocol::kPragueBlobSchedule;

using ChainConfig = protocol::PoSChainConfig;
using protocol::HeaderValidationResult;

using protocol::isForkActive;
using protocol::isForkBlock;
using protocol::blobScheduleFor;
using protocol::computeNextBaseFee;
using protocol::fakeExponential;
using protocol::computeNextExcessBlobGas;
using protocol::validateHeaderPoS;
}  // namespace bcos::devp2p::sync
