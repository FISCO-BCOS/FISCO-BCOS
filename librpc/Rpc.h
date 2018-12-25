/**
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
 *
 * @file Rpc.h
 * @author: caryliao
 * @date 2018-10-25
 */

#pragma once

#include "RpcFace.h"
#include <jsonrpccpp/common/exception.h>
#include <jsonrpccpp/server.h>
#include <libdevcrypto/Common.h>
#include <libledger/LedgerInterface.h>
#include <libledger/LedgerManager.h>
#include <libledger/LedgerParam.h>
#include <libp2p/P2PInterface.h>
#include <iostream>
#include <memory>
namespace dev
{
namespace rpc
{
/**
 * @brief JSON-RPC api
 */

class Rpc : public RpcFace
{
public:
    Rpc(std::shared_ptr<dev::ledger::LedgerManager> _ledgerManager,
        std::shared_ptr<dev::p2p::P2PInterface> _service);

    virtual RPCModules implementedModules() const override
    {
        return RPCModules{RPCModule{"FISCO BCOS", "2.0"}};
    }

    // system config part
    std::string getSystemConfigByKey(int _groupID, std::string const& key) override;

    // consensus part
    virtual std::string getBlockNumber(int _groupID) override;
    virtual std::string getPbftView(int _groupID) override;
    virtual Json::Value getMinerList(int _groupID) override;
    virtual Json::Value getObserverList(int _groupID) override;
    virtual Json::Value getConsensusStatus(int _groupID) override;

    // sync part
    virtual Json::Value getSyncStatus(int _groupID) override;

    // p2p part
    virtual std::string getClientVersion() override;
    virtual Json::Value getPeers() override;
    virtual Json::Value getGroupPeers(int _groupID) override;
    virtual Json::Value getGroupList() override;

    // block part
    virtual Json::Value getBlockByHash(
        int _groupID, const std::string& _blockHash, bool _includeTransactions) override;
    virtual Json::Value getBlockByNumber(
        int _groupID, const std::string& _blockNumber, bool _includeTransactions) override;
    virtual std::string getBlockHashByNumber(
        int _groupID, const std::string& _blockNumber) override;

    // transaction part
    virtual Json::Value getTransactionByHash(
        int _groupID, const std::string& _transactionHash) override;
    virtual Json::Value getTransactionByBlockHashAndIndex(
        int _groupID, const std::string& _blockHash, const std::string& _transactionIndex) override;
    virtual Json::Value getTransactionByBlockNumberAndIndex(int _groupID,
        const std::string& _blockNumber, const std::string& _transactionIndex) override;
    virtual Json::Value getTransactionReceipt(
        int _groupID, const std::string& _transactionHash) override;
    virtual Json::Value getPendingTransactions(int _groupID) override;
    virtual std::string getCode(int _groupID, const std::string& address) override;
    virtual Json::Value getTotalTransactionCount(int _groupID) override;
    virtual Json::Value call(int _groupID, const Json::Value& request) override;
    virtual std::string sendRawTransaction(int _groupID, const std::string& _rlp) override;

protected:
    std::shared_ptr<dev::ledger::LedgerManager> ledgerManager() { return m_ledgerManager; }
    std::shared_ptr<dev::ledger::LedgerManager> m_ledgerManager;
    std::shared_ptr<dev::p2p::P2PInterface> service() { return m_service; }
    std::shared_ptr<dev::p2p::P2PInterface> m_service;

private:
    bool isValidNodeId(dev::bytes const& precompileData,
        std::shared_ptr<dev::ledger::LedgerParamInterface> ledgerParam);
};

}  // namespace rpc
}  // namespace dev
