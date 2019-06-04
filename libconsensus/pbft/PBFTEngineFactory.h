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
 * (c) 2016-2019 fisco-dev contributors.
 */
/**
 * @brief : factory used to create PBFTEngine
 * @file: PBFTEngineFactory.h
 * @author: yujiechen
 *
 * @date: 2019-06-24
 *
 */
#pragma once
#include "PBFTEngine.h"
namespace dev
{
namespace consensus
{
class PBFTEngineFactoryInterface
{
public:
    PBFTEngineFactoryInterface() {}
    virtual ~PBFTEngineFactoryInterface() {}

    virtual std::shared_ptr<PBFTEngine> createPBFTEngine(
        std::shared_ptr<dev::p2p::P2PInterface> service,
        std::shared_ptr<dev::txpool::TxPoolInterface> txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> blockChain,
        std::shared_ptr<dev::sync::SyncInterface> blockSync,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> blockVerifier,
        dev::PROTOCOL_ID const& protocolId, std::string const& baseDir, KeyPair const& keyPair,
        h512s const& sealerList = h512s()) = 0;
};

class PBFTEngineFactory : public PBFTEngineFactoryInterface
{
public:
    PBFTEngineFactory() {}
    virtual ~PBFTEngineFactory() {}
    std::shared_ptr<PBFTEngine> createPBFTEngine(std::shared_ptr<dev::p2p::P2PInterface> service,
        std::shared_ptr<dev::txpool::TxPoolInterface> txPool,
        std::shared_ptr<dev::blockchain::BlockChainInterface> blockChain,
        std::shared_ptr<dev::sync::SyncInterface> blockSync,
        std::shared_ptr<dev::blockverifier::BlockVerifierInterface> blockVerifier,
        dev::PROTOCOL_ID const& protocolId, std::string const& baseDir, KeyPair const& keyPair,
        h512s const& sealerList = h512s()) override
    {
        std::shared_ptr<PBFTEngine> pbftEngine = std::make_shared<PBFTEngine>(service, txPool,
            blockChain, blockSync, blockVerifier, protocolId, baseDir, keyPair, sealerList);
        return pbftEngine;
    }
};

}  // namespace consensus
}  // namespace dev