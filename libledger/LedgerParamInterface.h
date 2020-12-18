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
 * @brief : external interface for the param of ledger
 * @file : LedgerParamInterface.h
 * @author: yujiechen
 * @date: 2018-10-23
 */
#pragma once
#include <libdevcrypto/Common.h>
#include <libprotocol/Common.h>
#include <boost/property_tree/ptree.hpp>
#include <memory>
#include <vector>
namespace bcos
{
namespace ledger
{
/// forward class declaration
struct TxPoolParam;
struct ConsensusParam;
struct BlockChainParam;
struct SyncParam;
struct P2pParam;
/// struct GenesisParam;
struct GenesisParam;
struct AMDBParam;
struct StorageParam;
struct StateParam;
struct TxParam;
struct EventLogFilterManagerParams;
struct FlowControlParam;
struct PermissionParam;

class LedgerParamInterface
{
public:
    LedgerParamInterface() = default;
    virtual ~LedgerParamInterface() {}
    virtual TxPoolParam& mutableTxPoolParam() = 0;
    virtual ConsensusParam& mutableConsensusParam() = 0;
    virtual SyncParam& mutableSyncParam() = 0;
    virtual GenesisParam& mutableGenesisParam() = 0;
    virtual AMDBParam& mutableAMDBParam() = 0;
    virtual std::string const& baseDir() const = 0;
    virtual void setBaseDir(std::string const& baseDir) = 0;
    virtual StorageParam& mutableStorageParam() = 0;
    virtual StateParam& mutableStateParam() = 0;
    virtual TxParam& mutableTxParam() = 0;
    virtual EventLogFilterManagerParams& mutableEventLogFilterManagerParams() = 0;
    virtual FlowControlParam& mutableFlowControlParam() = 0;
    virtual std::string& mutableGenesisMark() = 0;
    virtual PermissionParam& mutablePermissionParam() = 0;
    virtual std::string const& iniConfigPath() = 0;
    virtual std::string const& genesisConfigPath() = 0;
    virtual void parseSDKAllowList(bcos::h512s&, boost::property_tree::ptree const&) = 0;
};
}  // namespace ledger
}  // namespace bcos
