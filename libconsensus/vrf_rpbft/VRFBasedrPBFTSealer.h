/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2020 fisco-dev contributors.
 */
/**
 * @brief : implementation of VRF based rPBFTSealer
 * @file: VRFBasedrPBFTSealer.h
 * @author: yujiechen
 * @date: 2020-06-04
 */

#pragma once
#include "VRFBasedrPBFTEngine.h"
#include <libconsensus/TxGenerator.h>
#include <libconsensus/pbft/PBFTSealer.h>

#define VRFRPBFTSealer_LOG(LEVEL) \
    LOG(LEVEL) << LOG_BADGE("CONSENSUS") << LOG_BADGE("VRFBasedrPBFTSealer")

namespace dev
{
namespace consensus
{
class VRFBasedrPBFTSealer : public dev::consensus::PBFTSealer
{
public:
    using Ptr = std::shared_ptr<VRFBasedrPBFTSealer>;
    VRFBasedrPBFTSealer(std::shared_ptr<dev::txpool::TxPoolInterface> _txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        std::shared_ptr<dev::sync::SyncInterface> _blockSync);

    ~VRFBasedrPBFTSealer() override {}

    void initConsensusEngine() override;
    bool hookBeforeSealBlock() override;


protected:
    // generate and seal the workingSealerManagerPrecompiled transaction into _txOffset
    virtual bool generateAndSealerRotatingTx();

private:
    VRFBasedrPBFTEngine::Ptr m_vrfBasedrPBFTEngine;
    TxGenerator::Ptr m_txGenerator;
    // VRF public key
    std::string m_vrfPublicKey;

    unsigned const c_privateKeyLen = 32;
    std::string m_privateKey;
};
}  // namespace consensus
}  // namespace dev