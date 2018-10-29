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
 * @brief : concrete implementation of LedgerParamInterface
 * @file : LedgerParam.h
 * @author: yujiechen
 * @date: 2018-10-23
 */
#pragma once
#include "LedgerParamInterface.h"
#include <libdevcore/FixedHash.h>
#include <memory>
#include <vector>

#include "../libnetwork/Network.h"
namespace dev
{
namespace ledger
{
/// forward class declaration
struct TxPoolParam
{
    uint64_t txPoolLimit;
};
struct ConsensusParam
{
    std::string consensusType;
    dev::h512s minerList = dev::h512s();
    uint64_t maxTransactions;
    unsigned intervalBlockTime;
};

struct AMDBParam
{
    std::string topic;
    int retryInterval = 1;
    int maxRetry = 0;
};

struct SyncParam
{
    /// TODO: syncParam related
    unsigned idleWaitMs;
};
struct GenesisParam
{
    dev::h256 genesisHash;
};

class LedgerParam : public LedgerParamInterface
{
public:
    TxPoolParam& mutableTxPoolParam() override { return m_txPoolParam; }
    ConsensusParam& mutableConsensusParam() override { return m_consensusParam; }
    SyncParam& mutableSyncParam() override { return m_syncParam; }
    GenesisParam& mutableGenesisParam() override { return m_genesisParam; }
    AMDBParam& mutableAMDBParam() override { return m_amdbParam; }
    std::string const& dbType() const override { return m_dbType; }
    bool enableMpt() const override { return m_enableMpt; }
    void setMptState(bool mptState) override { m_enableMpt = mptState; }
    void setDBType(std::string const& dbType) override { m_dbType = dbType; }

    std::string const& baseDir() const override { return m_baseDir; }
    void setBaseDir(std::string const& baseDir) override { m_baseDir = baseDir; }

protected:
    virtual void initLedgerParams(){};

private:
    TxPoolParam m_txPoolParam;
    ConsensusParam m_consensusParam;
    SyncParam m_syncParam;
    GenesisParam m_genesisParam;
    AMDBParam m_amdbParam;
    std::string m_dbType;
    bool m_enableMpt;
    std::string m_baseDir;
};
}  // namespace ledger
}  // namespace dev
