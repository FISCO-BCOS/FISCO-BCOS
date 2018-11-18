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

    // consensus part
    virtual std::string blockNumber(int _groupID) override;
    virtual std::string pbftView(int _groupID) override;
    virtual Json::Value consensusStatus(int _groupID) override;

    // sync part
    virtual Json::Value syncStatus(int _groupID) override;

    // p2p part
    virtual std::string version() override;
    virtual Json::Value peers() override;
    virtual Json::Value groupPeers(int _groupID) override;
    virtual Json::Value groupList() override;

    // block part
    virtual Json::Value getBlockByHash(
        int _groupID, const std::string& _blockHash, bool _includeTransactions) override;
    virtual Json::Value getBlockByNumber(
        int _groupID, const std::string& _blockNumber, bool _includeTransactions) override;

    // transaction part
    /// @return the information about a transaction requested by transaction hash.
    virtual Json::Value getTransactionByHash(
        int _groupID, const std::string& _transactionHash) override;
    /// @return information about a transaction by block hash and transaction index position.
    virtual Json::Value getTransactionByBlockHashAndIndex(
        int _groupID, const std::string& _blockHash, const std::string& _transactionIndex) override;
    /// @return information about a transaction by block number and transaction index position.
    virtual Json::Value getTransactionByBlockNumberAndIndex(int _groupID,
        const std::string& _blockNumber, const std::string& _transactionIndex) override;
    /// @return the receipt of a transaction by transaction hash.
    /// @note That the receipt is not available for pending transactions.
    virtual Json::Value getTransactionReceipt(
        int _groupID, const std::string& _transactionHash) override;
    /// @return information about pendingTransactions.
    virtual Json::Value pendingTransactions(int _groupID) override;
    /// Executes a new message call immediately without creating a transaction on the blockchain.
    virtual std::string call(int _groupID, const Json::Value& request) override;
    // Creates new message call transaction or a contract creation for signed transactions.
    virtual std::string sendRawTransaction(int _groupID, const std::string& _rlp) override;

    std::string updatePBFTNode(int _groupID, const std::string& _rlp) override;

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
