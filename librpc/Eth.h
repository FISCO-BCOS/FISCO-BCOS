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
 * @brief: unit test for Session
 *
 * @file Eth.h
 * @author: caryliao
 * @date 2018-10-25
 */

#pragma once

#include "EthFace.h"
#include <jsonrpccpp/common/exception.h>
#include <jsonrpccpp/server.h>
#include <libdevcrypto/Common.h>
#include <libledger/LedgerInterface.h>
#include <libledger/LedgerManager.h>
#include <libp2p/Service.h>
#include <iostream>
#include <memory>

namespace dev
{
namespace rpc
{
/**
 * @brief JSON-RPC api
 */
class Eth : public EthFace
{
public:
    Eth(dev::ledger::LedgerManager<dev::ledger::LedgerInterface>& _ledgerManager,
        dev::p2p::Service& _service);

    virtual RPCModules implementedModules() const override
    {
        return RPCModules{RPCModule{"FISCO BCOS", "2.0"}};
    }

    // consensus part
    virtual Json::Value eth_blockNumber(const Json::Value& requestJson) override;
    virtual Json::Value eth_pbftView(const Json::Value& requestJson) override;

    // p2p part
    virtual Json::Value admin_peers(const Json::Value& requestJson) override;

    // block part
    virtual Json::Value eth_getBlockByHash(const Json::Value& requestJson) override;
    virtual Json::Value eth_getBlockByNumber(const Json::Value& requestJson) override;

    // transaction part
    /// @return the information about a transaction requested by transaction hash.
    virtual Json::Value eth_getTransactionByHash(const Json::Value& requestJson) override;
    /// @return information about a transaction by block hash and transaction index position.
    virtual Json::Value eth_getTransactionByBlockHashAndIndex(
        const Json::Value& requestJson) override;
    /// @return information about a transaction by block number and transaction index position.
    virtual Json::Value eth_getTransactionByBlockNumberAndIndex(
        const Json::Value& requestJson) override;
    /// @return the receipt of a transaction by transaction hash.
    /// @note That the receipt is not available for pending transactions.
    virtual Json::Value eth_getTransactionReceipt(const Json::Value& requestJson) override;
    /// @return information about pendingTransactions.
    virtual Json::Value eth_pendingTransactions(const Json::Value& requestJson) override;
    /// Executes a new message call immediately without creating a transaction on the blockchain.
    virtual Json::Value eth_call(const Json::Value& requestJson) override;

    // Creates new message call transaction or a contract creation for signed transactions.
    virtual Json::Value eth_sendRawTransaction(const Json::Value& requestJson) override;

    // amop part
    virtual Json::Value amop_topics(const Json::Value& requestJson) override;
    virtual Json::Value amop_setTopics(const Json::Value& requestJson) override;
    virtual Json::Value amop_sendMessage(const Json::Value& requestJson) override;

    // push event part
    //	virtual Json::Value eth_blockAccepted  (const Json::Value &requestJson) override;
    //	virtual Json::Value eth_transactionAccepted (const Json::Value &requestJson) override;
    //	virtual Json::Value amop_push (const Json::Value &requestJson) override;


protected:
    dev::ledger::LedgerManager<dev::ledger::LedgerInterface>* ledgerManager()
    {
        return &m_ledgerManager;
    }
    dev::ledger::LedgerManager<dev::ledger::LedgerInterface>& m_ledgerManager;
    dev::p2p::Service* service() { return &m_service; }
    dev::p2p::Service& m_service;
};

}  // namespace rpc
}  // namespace dev
