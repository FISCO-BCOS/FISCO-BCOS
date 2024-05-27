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

#include "RPBFTConfigTools.h"
#include <bcos-pbft/pbft/config/PBFTConfig.h>

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
        m_configTools(std::make_shared<RPBFTConfigTools>())
    {
        setConsensusType(ledger::ConsensusType::RPBFT_TYPE);
    }

    void resetConfig(
        bcos::ledger::LedgerConfig::Ptr _ledgerConfig, bool _syncedBlock = false) override;

    bool shouldRotateSealers(protocol::BlockNumber _number) const override;

    RPBFTConfigTools::Ptr rpbftConfigTools() const noexcept override { return m_configTools; }

private:
    RPBFTConfigTools::Ptr m_configTools;
};

}  // namespace bcos::consensus
