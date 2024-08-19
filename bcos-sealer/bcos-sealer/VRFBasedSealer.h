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
#include <bcos-utilities/Worker.h>
#include <utility>

namespace bcos::sealer
{
constexpr static const uint16_t curve25519PublicKeySize = 32;
constexpr static const uint16_t curve25519VRFProofSize = 96;
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

    // generate and seal the workingSealerManagerPrecompiled transaction into _txOffset
    static uint16_t generateTransactionForRotating(bcos::protocol::Block::Ptr& _block,
        SealerConfig::Ptr const&, SealingManager::ConstPtr const&, crypto::Hash::Ptr const&);
};
}  // namespace bcos::sealer