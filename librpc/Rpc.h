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
 * @author:darrenyin
 * @date  2019-09-20 add getTransactionByHashWithProof,getTransactionReceiptByHashWithProof for
 * wecross
 */

#pragma once

#include "RpcFace.h"            // for RpcFace
#include "libdevcore/Common.h"  // for bytes
#include "libp2p/Common.h"
#include "librpc/ModularServer.h"  // for ServerInterface<>::RPCModule, Serv...
#include <json/value.h>            // for Value
#include <libethcore/Transaction.h>
#include <boost/thread/tss.hpp>  // for thread_specific_ptr
#include <string>                // for string

namespace dev
{
namespace ledger
{
class LedgerManager;
class LedgerParamInterface;
}  // namespace ledger
namespace p2p
{
class P2PInterface;
}
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

    RPCModules implementedModules() const override
    {
        return RPCModules{RPCModule{"FISCO BCOS", "2.0"}};
    }

    // system config part
    std::string getSystemConfigByKey(int _groupID, const std::string& param2) override;

    // consensus part
    std::string getBlockNumber(int _groupID) override;
    std::string getPbftView(int _groupID) override;
    Json::Value getSealerList(int _groupID) override;
    Json::Value getObserverList(int _groupID) override;
    Json::Value getConsensusStatus(int _groupID) override;

    // sync part
    Json::Value getSyncStatus(int _groupID) override;

    // p2p part
    Json::Value getClientVersion() override;
    Json::Value getPeers(int _groupID) override;
    Json::Value getGroupPeers(int _groupID) override;
    Json::Value getGroupList() override;
    Json::Value getNodeIDList(int _groupID) override;

    // block part
    Json::Value getBlockByHash(
        int _groupID, const std::string& _blockHash, bool _includeTransactions) override;
    Json::Value getBlockByNumber(
        int _groupID, const std::string& _blockNumber, bool _includeTransactions) override;
    std::string getBlockHashByNumber(int _groupID, const std::string& _blockNumber) override;

    // transaction part
    Json::Value getTransactionByHash(int _groupID, const std::string& _transactionHash) override;
    Json::Value getTransactionByBlockHashAndIndex(
        int _groupID, const std::string& _blockHash, const std::string& _transactionIndex) override;
    Json::Value getTransactionByBlockNumberAndIndex(int _groupID, const std::string& _blockNumber,
        const std::string& _transactionIndex) override;
    Json::Value getTransactionReceipt(int _groupID, const std::string& _transactionHash) override;
    Json::Value getPendingTransactions(int _groupID) override;
    std::string getPendingTxSize(int _groupID) override;
    std::string getCode(int _groupID, const std::string& address) override;
    Json::Value getTotalTransactionCount(int _groupID) override;
    Json::Value call(int _groupID, const Json::Value& request) override;
    std::string sendRawTransaction(int _groupID, const std::string& _rlp) override;

    // Get transaction with merkle proof by hash
    Json::Value getTransactionByHashWithProof(
        int _groupID, const std::string& _transactionHash) override;
    // Get receipt with merkle proof by hash
    Json::Value getTransactionReceiptByHashWithProof(
        int _groupID, const std::string& _transactionHash) override;

    void setCurrentTransactionCallback(
        std::function<void(const std::string& receiptContext)>* _callback,
        std::function<uint32_t()>* _callbackVersion)
    {
        m_currentTransactionCallback.reset(_callback);
        m_transactionCallbackVersion.reset(_callbackVersion);
    }
    void clearCurrentTransactionCallback() { m_currentTransactionCallback.reset(NULL); }
    void setLedgerManager(std::shared_ptr<dev::ledger::LedgerManager> _ledgerManager)
    {
        m_ledgerManager = _ledgerManager;
    }
    void setService(std::shared_ptr<dev::p2p::P2PInterface> _service) { m_service = _service; }

protected:
    std::shared_ptr<dev::ledger::LedgerManager> ledgerManager();
    std::shared_ptr<dev::p2p::P2PInterface> service();
    std::shared_ptr<dev::ledger::LedgerManager> m_ledgerManager;
    std::shared_ptr<dev::p2p::P2PInterface> m_service;

private:
    bool isValidNodeId(dev::bytes const& precompileData,
        std::shared_ptr<dev::ledger::LedgerParamInterface> ledgerParam);
    bool isValidSystemConfig(std::string const& key);

    std::string buildReceipt(uint32_t clientProtocolVersion, int errorCode,
        const std::string& errorMessage, const bytes& input,
        dev::eth::LocalisedTransactionReceipt::Ptr receipt);

    /// transaction callback related
    std::function<std::function<void>()> setTransactionCallbackFactory();
    boost::thread_specific_ptr<std::function<void(const std::string& receiptContext)> >
        m_currentTransactionCallback;
    boost::thread_specific_ptr<std::function<uint32_t()> > m_transactionCallbackVersion;

    void checkRequest(int _groupID);
    void checkTxReceive(int _groupID);
};

}  // namespace rpc
}  // namespace dev
