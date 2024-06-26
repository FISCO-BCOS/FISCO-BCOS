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
 * @file genesisData.h
 * @author: wenlinli
 * @date 2022-10-24
 */

#pragma once

#include "Features.h"
#include "LedgerConfig.h"
#include "bcos-framework/consensus/ConsensusNodeInterface.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include "bcos-tool/VersionConverter.h"
#include <sstream>
#include <string>
#include <utility>


namespace bcos::ledger
{

struct FeatureSet
{
    Features::Flag flag{};
    int enable{};
};

struct Alloc
{
    using State = std::pair<std::string, std::string>;

    std::string address;
    u256 balance;
    std::string code;
    std::vector<State> storage;
};

class GenesisConfig
{
public:
    using Ptr = std::shared_ptr<GenesisConfig>;

    // chain config
    bool m_smCrypto{};
    std::string m_chainID;
    std::string m_groupID;

    // web3 chain config
    std::string m_web3ChainID;

    // consensus config
    std::string m_consensusType;
    uint64_t m_txCountLimit = 1000;
    uint64_t m_leaderSwitchPeriod = 1;

    // version config
    uint32_t m_compatibilityVersion;

    // tx config
    uint64_t m_txGasLimit = 3000000000;
    // executorConfig
    bool m_isWasm{};
    bool m_isAuthCheck = true;
    std::string m_authAdminAccount;
    bool m_isSerialExecute = true;

    // rpbft config
    int64_t m_epochSealerNum = 4;
    int64_t m_epochBlockNum = 1000;

    std::vector<FeatureSet> m_features;
    std::vector<Alloc> m_allocs;

};  // namespace genesisConfig
}  // namespace bcos::ledger
