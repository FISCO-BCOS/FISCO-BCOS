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

#include "LedgerConfig.h"
#include "bcos-framework/consensus/ConsensusNodeInterface.h"
#include "bcos-ledger/src/libledger/Ledger.h"
#include <sstream>
#include <string>


namespace bcos
{
namespace ledger
{
class GenesisConfig
{
public:
    using Ptr = std::shared_ptr<GenesisConfig>;
    GenesisConfig(bool smcrypto, std::string chainID, std::string groupID,
        std::string consensusType, uint64_t blockLimit, uint64_t leaderSwitchPeriod,
        std::string compatibilityVersion, uint64_t txGasLimit, bool isWasm, bool isAuthCheck,
        std::string authAdminAccount, bool isSerialExecute)
      : m_smCrypto(smcrypto),
        m_chainID(chainID),
        m_groupID(groupID),
        m_consensusType(consensusType),
        m_blockLimit(blockLimit),
        m_leaderSwitchPeriod(leaderSwitchPeriod),
        m_compatibilityVersion(compatibilityVersion),
        m_txGasLimit(txGasLimit),
        m_isWasm(isWasm),
        m_isAuthCheck(isAuthCheck),
        m_authAdminAccount(authAdminAccount),
        m_isSerialExecute(isSerialExecute){};
    virtual ~GenesisConfig() {}

    std::string genesisDataOutPut()
    {
        std::stringstream ss;
        ss << "[chian]" << std::endl
           << "sm_crypto:" << m_smCrypto << std::endl
           << "chainID: " << m_chainID << std::endl
           << "grouID: " << m_groupID << std::endl
           << "[consensys]" << std::endl
           << "consensus_type: " << m_consensusType << std::endl
           << "block_tx_count_limit:" << m_blockLimit << std::endl
           << "leader_period:" << m_leaderSwitchPeriod << std::endl
           << "[version]" << std::endl
           << "compatibility_version:" << m_compatibilityVersion << std::endl
           << "[tx]" << std::endl
           << "gaslimit:" << m_txGasLimit << std::endl
           << "[executor]" << std::endl
           << "iswasm: " << m_isWasm << std::endl
           << "isAuthCheck:" << m_isAuthCheck << std::endl
           << "authAdminAccount:" << m_authAdminAccount << std::endl
           << "isSerialExecute:" << m_isSerialExecute << std::endl;
        return ss.str();
    }


private:
    // chain config
    bool m_smCrypto;
    std::string m_chainID;
    std::string m_groupID;

    // consensus config
    std::string m_consensusType;
    uint64_t m_blockLimit;
    uint64_t m_leaderSwitchPeriod = 1;

    // version config
    std::string m_compatibilityVersion;

    // tx config
    uint64_t m_txGasLimit = 3000000000;
    // executorConfig
    bool m_isWasm;
    bool m_isAuthCheck;
    std::string m_authAdminAccount;
    bool m_isSerialExecute;
};  // namespace genesisConfig
}  // namespace ledger
}  // namespace bcos
