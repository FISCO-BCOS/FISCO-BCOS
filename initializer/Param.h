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
 * (c) 2016-2018 fisco-dev contributors.
 */

/**
 * @brief : concrete implementation of ParamInterface
 * @file : Param.h
 * @author: yujiechen
 * @date: 2018-10-23
 */
#pragma once
#include "ParamInterface.h"
#include <libdevcore/FixedHash.h>
#include <libdevcrypto/Common.h>
#include <libp2p/Network.h>
#include <memory>
#include <vector>
namespace dev
{
namespace initializer
{
/// forward class declaration
struct TxPoolParam
{
    uint64_t txPoolLimit;
};
struct ConsensusParam
{
    std::string consensusType;
    dev::h512s minerList;
    unsigned intervalBlockTime;
};
struct BlockChainParam
{
    /// TODO: gensis related config
};

struct SyncParam
{
    /// TODO: syncParam related
    unsigned idleWaitMs;
};

struct P2pParam
{
    dev::p2p::NetworkConfig networkConfig;
};

struct GenesisParam
{
    dev::h256 genesisHash;
};

class LedgerParam : public LedgerParamInterface
{
public:
    TxPoolParam const& txPoolParam() const override { return m_txPoolParam; }
    ConsensusParam const& consensusParam() const override { return m_consensusParam; }
    BlockChainParam const& blockChainParam() const override { return m_blockChainParam; }
    SyncParam const& syncParam() const override { return m_syncParam; }
    dev::eth::GroupID const& groupId() const override { return m_groupId; }
    std::string const& baseDir() const override { return m_baseDir; }
    dev::KeyPair const& keyPair() const override { return m_keypair; }
    GenesisParam const& genesisParam() const override { return m_genesisParam; }

protected:
    virtual void initLedgerParams(){};

private:
    TxPoolParam m_txPoolParam;
    ConsensusParam m_consensusParam;
    BlockChainParam m_blockChainParam;
    SyncParam m_syncParam;
    GenesisParam m_genesisParam;
    dev::eth::GroupID m_groupId;
    std::string m_baseDir;
    dev::KeyPair m_keypair;
};
}  // namespace initializer
}  // namespace dev
