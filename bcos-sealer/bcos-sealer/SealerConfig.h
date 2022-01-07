/*
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
 * @file SealerConfig.h
 * @author: yujiechen
 * @date: 2021-05-14
 */
#pragma once
#include "bcos-framework/interfaces/consensus/ConsensusInterface.h"
#include "bcos-framework/interfaces/protocol/BlockFactory.h"
#include "bcos-framework/interfaces/txpool/TxPoolInterface.h"
namespace bcos
{
namespace sealer
{
class SealerConfig
{
public:
    using Ptr = std::shared_ptr<SealerConfig>;
    SealerConfig(
        bcos::protocol::BlockFactory::Ptr _blockFactory, bcos::txpool::TxPoolInterface::Ptr _txpool)
      : m_txpool(_txpool), m_blockFactory(_blockFactory)
    {}
    virtual ~SealerConfig() {}

    virtual void setConsensusInterface(bcos::consensus::ConsensusInterface::Ptr _consensus)
    {
        m_consensus = _consensus;
    }
    virtual bcos::txpool::TxPoolInterface::Ptr txpool() { return m_txpool; }

    virtual unsigned minSealTime() const { return m_minSealTime; }
    virtual void setMinSealTime(unsigned _minSealTime) { m_minSealTime = _minSealTime; }

    bcos::protocol::BlockFactory::Ptr blockFactory() { return m_blockFactory; }
    bcos::consensus::ConsensusInterface::Ptr consensus() { return m_consensus; }

protected:
    bcos::txpool::TxPoolInterface::Ptr m_txpool;
    bcos::protocol::BlockFactory::Ptr m_blockFactory;
    bcos::consensus::ConsensusInterface::Ptr m_consensus;
    unsigned m_minSealTime = 500;
};
}  // namespace sealer
}  // namespace bcos