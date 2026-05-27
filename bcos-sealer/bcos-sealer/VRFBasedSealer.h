/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file VRFBasedSealer.h
 * @author: kyonGuo
 * @date 2023/7/5
 */

#pragma once
#include "Sealer.h"
#include "SealerConfig.h"
#include "SealingManager.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/sealer/VrfCurveType.h"
#include <bcos-utilities/Worker.h>
#include <utility>

namespace bcos::sealer
{
constexpr static const uint16_t curve25519PublicKeySize = 32;
constexpr static const uint16_t curve25519VRFProofSize = 96;
constexpr static const uint16_t secp256k1PublicKeySize = 33;
constexpr static const uint16_t secp256k1VRFProofSize = 97;
class VRFBasedSealer : public bcos::sealer::Sealer
{
public:
    explicit VRFBasedSealer(bcos::sealer::SealerConfig::Ptr _config)
      : bcos::sealer::Sealer(std::move(_config))
    {}
    ~VRFBasedSealer() override = default;
    VRFBasedSealer(const VRFBasedSealer&) = delete;
    VRFBasedSealer& operator=(const VRFBasedSealer&) = delete;
    VRFBasedSealer(VRFBasedSealer&&) = delete;
    VRFBasedSealer& operator=(VRFBasedSealer&&) = delete;

    uint16_t hookWhenSealBlock(bcos::protocol::Block::Ptr _block) override;

    // FIB-160: legacy convenience wrapper -- reads features through the synchronized
    // ConsensusConfig::features() (which now takes a shared_lock). For race-free
    // multi-flag selection in the same generation pass, use the (Features) overload.
    static sealer::VrfCurveType getVrfCurveType(SealerConfig::Ptr const& _sealerConfig);
    // FIB-160: snapshot-friendly overload. Takes the features bitset already copied
    // out (typically from ConsensusConfig::getRotationSnapshot) so the curve choice
    // is computed against the same view as the rotate decision and other flag
    // reads in the same call.
    static sealer::VrfCurveType getVrfCurveType(ledger::Features const& features);

    // generate and seal the workingSealerManagerPrecompiled transaction into _txOffset
    // Legacy entry point: reads features fresh inside (curve choice may not be
    // snapshot-consistent with the caller's earlier reads).
    [[deprecated(
        "Use the Features-snapshot overload (FIB-160). The bool overload re-reads features "
        "and breaks snapshot consistency.")]] static uint16_t
    generateTransactionForRotating(bcos::protocol::Block::Ptr& _block, SealerConfig::Ptr const&,
        SealingManager::ConstPtr const&, crypto::Hash::Ptr const&, bool blockNumberInput);
    // FIB-160: snapshot-aware overload. Caller supplies a single Features view so
    // both the curve choice and the blockNumberInput flag come from the same
    // snapshot (avoids cross-read races during VRF mode selection).
    static uint16_t generateTransactionForRotating(bcos::protocol::Block::Ptr& _block,
        SealerConfig::Ptr const&, SealingManager::ConstPtr const&, crypto::Hash::Ptr const&,
        ledger::Features const& features);
};
}  // namespace bcos::sealer