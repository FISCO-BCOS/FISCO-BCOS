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

using namespace bcos;
using namespace bcos::consensus;

void RPBFTConfig::resetConfig(bcos::ledger::LedgerConfig::Ptr _ledgerConfig, bool _syncedBlock)
{
    PBFTConfig::resetConfig(_ledgerConfig, _syncedBlock);
    m_configTools->resetConfig(_ledgerConfig);
}

bool RPBFTConfig::shouldRotateSealers(protocol::BlockNumber _number) const
{
    return m_configTools->shouldRotateSealers(_number);
}
