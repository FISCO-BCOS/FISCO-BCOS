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
 * @file RPBFTConfig.h
 * @author: chuwen
 * @date 2023-04-19
 */

#pragma once

#include "bcos-pbft/bcos-pbft/pbft/config/PBFTConfig.h"

namespace bcos::consensus
{

class RPBFTConfig : public PBFTConfig
{
public:
    using Ptr = std::shared_ptr<RPBFTConfig>;

    RPBFTConfig(bcos::crypto::CryptoSuite::Ptr _cryptoSuite,
        bcos::crypto::KeyPairInterface::Ptr _keyPair,
        std::shared_ptr<PBFTMessageFactory> _pbftMessageFactory,
        std::shared_ptr<PBFTCodecInterface> _codec, std::shared_ptr<ValidatorInterface> _validator,
        std::shared_ptr<bcos::front::FrontServiceInterface> _frontService,
        StateMachineInterface::Ptr _stateMachine, PBFTStorage::Ptr _storage,
        bcos::protocol::BlockFactory::Ptr _blockFactory)
      : PBFTConfig(std::move(_cryptoSuite), std::move(_keyPair), std::move(_pbftMessageFactory),
            std::move(_codec), std::move(_validator), std::move(_frontService),
            std::move(_stateMachine), std::move(_storage), std::move(_blockFactory)),
        m_workingSealerNodeList(std::make_shared<consensus::ConsensusNodeList>())
    {}

    void resetConfig(
        bcos::ledger::LedgerConfig::Ptr _ledgerConfig, bool _syncedBlock = false) override;

    void updateWorkingSealerNodeList(const bcos::ledger::LedgerConfig::Ptr& _ledgerConfig);

    void updateShouldRotateSealers(const bcos::ledger::LedgerConfig::Ptr& _ledgerConfig);

    void updateEpochBlockNum(const bcos::ledger::LedgerConfig::Ptr& _ledgerConfig);
    [[nodiscard]] bool updateEpochSealerNum(const bcos::ledger::LedgerConfig::Ptr& _ledgerConfig);
    void updateNotifyRotateFlag(const bcos::ledger::LedgerConfig::Ptr& _ledgerConfig);

    void setShouldRotateSealers(bool _shouldRotateSealers);
    bool shouldRotateSealers() const override;

private:
    // the node index in working consensus node list
    std::atomic<IndexType> m_nodeIndexInWorkingSealer{0};
    // the number of working consensus nodes
    std::atomic_uint64_t m_workingSealerNodeNum{0};

    // working consensus node list
    ConsensusNodeListPtr m_workingSealerNodeList;
    std::atomic_bool m_workingSealerNodeListUpdated{false};
    std::atomic_bool m_shouldRotateWorkingSealer{false};

    // epoch block num
    std::atomic_uint64_t m_epochBlockNum{0};
    bcos::protocol::BlockNumber m_epochBlockNumEnableNumber{0};

    // epoch consensus node size
    std::atomic_uint64_t m_epochSealerNum{0};
    bcos::protocol::BlockNumber m_epochSealerNumEnableNumber{0};

    std::atomic_uint64_t m_notifyRotateFlag{0};
};

}  // namespace bcos::consensus
