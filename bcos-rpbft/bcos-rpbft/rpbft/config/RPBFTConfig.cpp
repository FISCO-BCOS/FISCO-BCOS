/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief config for RPBFT
 * @file RPBFTConfig.cpp
 * @author: chuwen
 * @date 2023-04-19
 */

#include "RPBFTConfig.h"

#include "../utilities/Common.h"
#include <bcos-crypto/KeyCompareTools.h>

using namespace bcos;
using namespace bcos::consensus;

void RPBFTConfig::resetConfig(bcos::ledger::LedgerConfig::Ptr _ledgerConfig, bool _syncedBlock)
{
}

void RPBFTConfig::updateWorkingSealerNodeList(bcos::ledger::LedgerConfig::Ptr _ledgerConfig)
{
}

void RPBFTConfig::updateShouldRotateSealers(bcos::ledger::LedgerConfig::Ptr _ledgerConfig)
{
}

void RPBFTConfig::updateEpochBlockNum(bcos::ledger::LedgerConfig::Ptr _ledgerConfig)
{
    m_epochBlockNum = std::get<0>(_ledgerConfig->epochBlockNum());
    m_epochBlockNumEnableNumber = std::get<1>(_ledgerConfig->epochBlockNum());
}

void RPBFTConfig::updateEpochSealerNum(
    bcos::ledger::LedgerConfig::Ptr _ledgerConfig, bool& isEpochSealerNumChanged)
{
    std::uint64_t originEpochSealerNum = m_epochSealerNum;

    if (std::get<0>(_ledgerConfig->epochBlockNum()) == originEpochSealerNum)
    {
        isEpochSealerNumChanged = false;
        return;
    }

    m_epochSealerNum = std::get<0>(_ledgerConfig->epochSealerNum()) > m_consensusNodeList->size() ?
                       m_consensusNodeList->size() :
                       std::get<0>(_ledgerConfig->epochSealerNum());
    m_epochSealerNumEnableNumber = std::get<1>(_ledgerConfig->epochSealerNum());

    isEpochSealerNumChanged =
            (static_cast<std::uint64_t>(originEpochSealerNum) != m_epochSealerNum);
}

void RPBFTConfig::updateNotifyRotateFlag(bcos::ledger::LedgerConfig::Ptr _ledgerConfig)
{
    m_notifyRotateFlag = _ledgerConfig->notifyRotateFlagInfo();
}

bool RPBFTConfig::shouldRotateSealers() const
{
    return m_shouldRotateWorkingSealer;
}

void RPBFTConfig::setShouldRotateSealers(const bool _shouldRotateSealers)
{
    m_shouldRotateWorkingSealer = _shouldRotateSealers;
}