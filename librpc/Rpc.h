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
#include "Common.h"
#include "RpcFace.h"            // for RpcFace
#include "libdevcore/Common.h"  // for bytes
#include "libp2p/Common.h"
#include "librpc/ModularServer.h"  // for ServerInterface<>::RPCModule, Serv...
#include <json/value.h>            // for Value
#include <libethcore/Transaction.h>
#include <libinitializer/LedgerInitializer.h>
#include <libledger/LedgerManager.h>
#include <libprecompiled/Common.h>
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
// for dynamic group management
namespace LedgerManagementStatusCode
{
const std::string SUCCESS = "0x0";
const std::string INTERNAL_ERROR = "0x1";
const std::string GROUP_ALREADY_EXISTS = "0x2";
const std::string GROUP_ALREADY_RUNNING = "0x3";
const std::string GROUP_ALREADY_STOPPED = "0x4";
const std::string GROUP_ALREADY_DELETED = "0x5";
const std::string GROUP_NOT_FOUND = "0x6";
const std::string INVALID_PARAMS = "0x7";
const std::string PEERS_NOT_CONNECTED = "0x8";
const std::string GENESIS_CONF_ALREADY_EXISTS = "0x9";
const std::string GROUP_CONF_ALREADY_EXIST = "0xa";
const std::string GENESIS_CONF_NOT_FOUND = "0xb";
const std::string GROUP_CONF_NOT_FOUND = "0xc";
const std::string GROUP_IS_STOPPING = "0xd";
const std::string GROUP_HAS_NOT_DELETED = "0xe";
}  // namespace LedgerManagementStatusCode

/**
 * @brief JSON-RPC api
 */

class Rpc : public RpcFace
{
public:
    Rpc(dev::initializer::LedgerInitializer::Ptr _ledgerInitializer,
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
    Json::Value getEpochSealersList(int _groupID) override;
    Json::Value getObserverList(int _groupID) override;
    Json::Value getConsensusStatus(int _groupID) override;

    // sync part
    Json::Value getSyncStatus(int _groupID) override;

    // p2p part
    Json::Value getClientVersion() override;
    Json::Value getPeers(int) override;
    Json::Value getGroupPeers(int _groupID) override;
    Json::Value getGroupList() override;
    Json::Value getNodeIDList(int _groupID) override;
    Json::Value getAmopTopicSubscribers(const std::string& _topicName) override;

    // block part
    Json::Value getBlockByHash(
        int _groupID, const std::string& _blockHash, bool _includeAllData) override;
    Json::Value getBlockByNumber(
        int _groupID, const std::string& _blockNumber, bool _includeAllData) override;

    Json::Value getBlockHeaderByNumber(
        int _groupID, const std::string& _blockNumber, bool _includeSigList = false) override;
    Json::Value getBlockHeaderByHash(
        int _groupID, const std::string& _blockHash, bool _includeSigList = false) override;

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
    std::string sendRawTransactionAndGetProof(int _groupID, const std::string& _rlp) override;

    // Get transaction with merkle proof by hash
    Json::Value getTransactionByHashWithProof(
        int _groupID, const std::string& _transactionHash) override;
    // Get receipt with merkle proof by hash
    Json::Value getTransactionReceiptByHashWithProof(
        int _groupID, const std::string& _transactionHash) override;

    Json::Value generateGroup(int _groupID, const Json::Value& _params) override;
    Json::Value startGroup(int _groupID) override;
    Json::Value stopGroup(int _groupID) override;
    Json::Value removeGroup(int _groupID) override;
    Json::Value recoverGroup(int _groupID) override;
    Json::Value queryGroupStatus(int _groupID) override;

    Json::Value getBatchReceiptsByBlockNumberAndRange(int _groupID, const std::string& _blockNumber,
        std::string const& _from, std::string const& _count, bool compress = true) override;
    Json::Value getBatchReceiptsByBlockHashAndRange(int _groupID, const std::string& _blockHash,
        std::string const& _from, std::string const& _count, bool compress = true) override;


    void setCurrentTransactionCallback(
        std::function<void(const std::string& receiptContext, GROUP_ID _groupId)>* _callback,
        std::function<uint32_t()>* _callbackVersion)
    {
        m_currentTransactionCallback.reset(_callback);
        m_transactionCallbackVersion.reset(_callbackVersion);
    }
    void clearCurrentTransactionCallback() { m_currentTransactionCallback.reset(NULL); }
    void setLedgerInitializer(dev::initializer::LedgerInitializer::Ptr _ledgerInitializer)
    {
        m_ledgerInitializer = _ledgerInitializer;
        if (_ledgerInitializer != nullptr)
        {
            m_ledgerManager = _ledgerInitializer->ledgerManager();
        }
    }
    void setService(std::shared_ptr<dev::p2p::P2PInterface> _service) { m_service = _service; }

protected:
    std::shared_ptr<dev::ledger::LedgerManager> ledgerManager();
    std::shared_ptr<dev::p2p::P2PInterface> service();

    std::string sendRawTransaction(int _groupID, const std::string& _rlp,
        std::function<std::shared_ptr<Json::Value>(
            std::weak_ptr<dev::blockchain::BlockChainInterface> _blockChain,
            LocalisedTransactionReceipt::Ptr receipt, dev::bytesConstRef input,
            dev::eth::Block::Ptr _blockPtr)>
            _notifyCallback);

    std::shared_ptr<Json::Value> notifyReceipt(
        std::weak_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        LocalisedTransactionReceipt::Ptr receipt, dev::bytesConstRef input,
        dev::eth::Block::Ptr _blockPtr);

    std::shared_ptr<Json::Value> notifyReceiptWithProof(
        std::weak_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        LocalisedTransactionReceipt::Ptr receipt, dev::bytesConstRef input,
        dev::eth::Block::Ptr _blockPtr);

    void addProofToResponse(std::shared_ptr<Json::Value> _response, std::string const& _key,
        std::shared_ptr<dev::blockchain::MerkleProofType> _proofList);

    void generateBlockHeaderInfo(Json::Value& _response, dev::eth::BlockHeader const& _blockHeader,
        dev::eth::Block::SigListPtrType _signatureList, bool _includeSigList);


    std::shared_ptr<dev::ledger::LedgerManager> m_ledgerManager;
    dev::initializer::LedgerInitializer::Ptr m_ledgerInitializer;

    std::shared_ptr<dev::p2p::P2PInterface> m_service;

    const std::set<std::string> c_supportedSystemConfigKeys = {
        dev::precompiled::SYSTEM_KEY_TX_COUNT_LIMIT, dev::precompiled::SYSTEM_KEY_TX_GAS_LIMIT,
        dev::precompiled::SYSTEM_KEY_RPBFT_EPOCH_BLOCK_NUM,
        dev::precompiled::SYSTEM_KEY_RPBFT_EPOCH_SEALER_NUM,
        dev::precompiled::SYSTEM_KEY_CONSENSUS_TIMEOUT};

private:
    bool isValidNodeId(dev::bytes const& precompileData,
        std::shared_ptr<dev::ledger::LedgerParamInterface> ledgerParam);
    bool isValidSystemConfig(std::string const& key);

    template <typename T>
    void checkLedgerStatus(T _modulePtr, std::string const& _moduleName, std::string _method)
    {
        // the module is not initialized well
        if (!_modulePtr)
        {
            RPC_LOG(WARNING) << LOG_DESC(
                _method + ": get " + _moduleName + " failed, please check the status of the group");
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::GroupID, RPCMsg[RPCExceptionType::GroupID]));
        }
    }

    /// transaction callback related
    boost::thread_specific_ptr<
        std::function<void(const std::string& receiptContext, GROUP_ID _groupId)>>
        m_currentTransactionCallback;
    boost::thread_specific_ptr<std::function<uint32_t()>> m_transactionCallbackVersion;

    void checkRequest(int _groupID);
    void checkSyncStatus(int _groupID);

    void checkNodeVersionForGroupMgr(const char* _methodName);
    bool checkParamsForGenerateGroup(
        const Json::Value& _params, ledger::GroupParams& _groupParams, Json::Value& _response);
    bool checkGroupIDForGroupMgr(int _groupID, Json::Value& _response);
    bool checkSealerID(const std::string& _sealer);
    bool checkTimestamp(const std::string& _timestamp);
    bool checkConnection(const std::set<std::string>& _sealerList, Json::Value& _response);

    void parseTransactionIntoResponse(Json::Value& _response, dev::h256 const& _blockHash,
        int64_t _blockNumber, int64_t _txIndex, Transaction::Ptr _tx, bool onChain = true);

    void parseReceiptIntoResponse(Json::Value& _response, dev::bytesConstRef _input,
        dev::eth::LocalisedTransactionReceipt::Ptr _receipt);

    void parseSignatureIntoResponse(
        Json::Value& _response, dev::eth::Block::SigListPtrType _signatureList);

    void getBatchReceipts(Json::Value& _response, dev::eth::Block::Ptr _block,
        std::string const& _from, std::string const& _size, bool _compress);
};

}  // namespace rpc
}  // namespace dev
