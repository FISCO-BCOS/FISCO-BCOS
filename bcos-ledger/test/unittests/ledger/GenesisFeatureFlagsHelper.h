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
 * @file GenesisFeatureFlagsHelper.h
 * @brief Test helper: append the SystemConfig feature_flags Entry slot to an
 *        L2 genesis alloc — the value Ledger::buildGenesisBlock VERIFIES (it
 *        no longer injects it; the slot must arrive in the allocs so the
 *        genesis state root commits it).
 */
#pragma once
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/ledger/GenesisConfig.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <algorithm>
#include <array>
#include <string>

namespace bcos::test
{
// Compute the packed feature_flags Entry word (value = Features of the
// genesis config, enableNumber = 0) exactly as Ledger::importGenesisState
// expects it, and append it to the SystemConfig predeploy alloc (address
// 0x43...00c0) of @p genesis. No-op if that alloc is absent.
inline void appendGenesisFeatureFlagsSlot(bcos::ledger::GenesisConfig& genesis)
{
    constexpr std::string_view c_systemConfigAddress = "43000000000000000000000000000000000000c0";
    auto allocIt = std::find_if(
        genesis.m_allocs.begin(), genesis.m_allocs.end(), [&](bcos::ledger::Alloc const& alloc) {
            std::string address =
                alloc.address.starts_with("0x") ? alloc.address.substr(2) : alloc.address;
            std::transform(address.begin(), address.end(), address.begin(),
                [](unsigned char c) { return std::tolower(c); });
            return address == c_systemConfigAddress;
        });
    if (allocIt == genesis.m_allocs.end())
    {
        return;
    }

    // Replicate the feature set buildGenesisBlock persists: version-gated
    // genesis defaults plus the explicitly enabled genesis features.
    bcos::ledger::Features features;
    features.setGenesisFeatures(
        static_cast<bcos::protocol::BlockVersion>(genesis.m_compatibilityVersion));
    // buildGenesisBlock auto-sets feature_rpbft for rpbft chains (>= 3.5);
    // replicate it so an rpbft L2 fixture would not false-mismatch.
    if (genesis.m_consensusType == bcos::ledger::RPBFT_CONSENSUS_TYPE &&
        genesis.m_compatibilityVersion >=
            static_cast<uint32_t>(bcos::protocol::BlockVersion::V3_5_VERSION))
    {
        features.set(bcos::ledger::Features::Flag::feature_rpbft);
    }
    for (auto const& featureSet : genesis.m_features)
    {
        if (featureSet.enable > 0)
        {
            features.set(featureSet.flag);
        }
    }

    // slot = keccak256(utf8("feature_flags") || be32(101))
    bcos::bytes slotInput;
    constexpr std::string_view c_key = "feature_flags";
    slotInput.insert(slotInput.end(), c_key.begin(), c_key.end());
    bcos::bytes baseSlot(32, 0);
    baseSlot[31] = 101;
    slotInput.insert(slotInput.end(), baseSlot.begin(), baseSlot.end());
    auto slotHash = bcos::crypto::keccak256Hash(bcos::ref(slotInput));

    // value = packed flags number, 32-byte big-endian, enableNumber = 0
    std::array<uint8_t, 32> word{};
    auto flagsNumber = features.toFlagsNumber();
    for (size_t i = 0; i < word.size(); ++i)
    {
        word[word.size() - 1 - i] = (flagsNumber & 0xFF).convert_to<uint8_t>();
        flagsNumber >>= 8;
    }

    allocIt->storage.emplace_back(
        slotHash.hex(), bcos::toHex(bcos::bytesConstRef(word.data(), word.size())));
}
}  // namespace bcos::test
