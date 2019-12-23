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
 * along with FISCO-BCOS.  If not, see <http:www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 *
 * @file Rpc.cpp
 * @author: caryliao
 * @date 2018-10-25
 */

#include "Rpc.h"
#include "Common.h"
#include "JsonHelper.h"
#include "libledger/LedgerManager.h"  // for LedgerManager
#include <jsonrpccpp/common/exception.h>
#include <jsonrpccpp/server.h>
#include <libconfig/GlobalConfigure.h>
#include <libdevcore/CommonData.h>
#include <libethcore/Common.h>
#include <libethcore/CommonJS.h>
#include <libethcore/Transaction.h>
#include <libexecutive/ExecutionResult.h>
#include <libinitializer/GroupGenerator.h>
#include <libprecompiled/SystemConfigPrecompiled.h>
#include <libsync/SyncStatus.h>
#include <libtxpool/TxPoolInterface.h>
#include <boost/algorithm/hex.hpp>
#include <csignal>

using namespace jsonrpc;
using namespace dev::rpc;
using namespace dev::sync;
using namespace dev::ledger;
using namespace dev::precompiled;
using namespace dev::initializer;

static const int64_t maxTransactionGasLimit = 0x7fffffffffffffff;
static const int64_t gasPrice = 1;

std::map<int, std::string> dev::rpc::RPCMsg{{RPCExceptionType::Success, "Success"},
    {RPCExceptionType::GroupID, "GroupID does not exist"},
    {RPCExceptionType::JsonParse, "Response json parse error"},
    {RPCExceptionType::BlockHash, "BlockHash does not exist"},
    {RPCExceptionType::BlockNumberT, "BlockNumber does not exist"},
    {RPCExceptionType::TransactionIndex, "TransactionIndex is out of range"},
    {RPCExceptionType::CallFrom, "Call needs a 'from' field"},
    {RPCExceptionType::NoView, "Only pbft consensus supports the view property"},
    {RPCExceptionType::InvalidSystemConfig, "Invalid System Config"},
    {RPCExceptionType::InvalidRequest,
        "Don't send request to this node who doesn't belong to the group"},
    {RPCExceptionType::IncompleteInitialization, "RPC module initialization is incomplete."}};

Rpc::Rpc(
    LedgerInitializer::Ptr _ledgerInitializer, std::shared_ptr<dev::p2p::P2PInterface> _service)
  : m_service(_service)
{
    setLedgerInitializer(_ledgerInitializer);
}

void Rpc::registerSyncChecker()
{
    if (m_ledgerManager)
    {
        auto groupList = m_ledgerManager->getGroupListForRpc();
        for (auto const& group : groupList)
        {
            auto txPool = m_ledgerManager->txPool(group);
            txPool->registerSyncStatusChecker([this, group]() {
                try
                {
                    checkSyncStatus(group);
                }
                catch (std::exception const& _e)
                {
                    return false;
                }
                return true;
            });
        }
    }
}

std::shared_ptr<dev::ledger::LedgerManager> Rpc::ledgerManager()
{
    if (!m_ledgerManager)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(RPCExceptionType::IncompleteInitialization,
            RPCMsg[RPCExceptionType::IncompleteInitialization]));
    }
    return m_ledgerManager;
}

std::shared_ptr<dev::p2p::P2PInterface> Rpc::service()
{
    if (!m_service)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(RPCExceptionType::IncompleteInitialization,
            RPCMsg[RPCExceptionType::IncompleteInitialization]));
    }
    return m_service;
}

bool Rpc::isValidSystemConfig(std::string const& key)
{
    return (key == SYSTEM_KEY_TX_COUNT_LIMIT || key == SYSTEM_KEY_TX_GAS_LIMIT);
}

void Rpc::checkRequest(int _groupID)
{
    if (!m_service || !m_ledgerManager)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(RPCExceptionType::IncompleteInitialization,
            RPCMsg[RPCExceptionType::IncompleteInitialization]));
    }
    auto blockchain = ledgerManager()->blockChain(_groupID);
    if (!blockchain)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(RPCExceptionType::GroupID, RPCMsg[RPCExceptionType::GroupID]));
    }
    auto _nodeList = blockchain->sealerList() + blockchain->observerList();
    auto it = std::find(_nodeList.begin(), _nodeList.end(), service()->id());
    if (it == _nodeList.end())
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(
            RPCExceptionType::InvalidRequest, RPCMsg[RPCExceptionType::InvalidRequest]));
    }
    return;
}

void Rpc::checkSyncStatus(int _groupID)
{
    // Refuse transaction if far syncing
    auto syncModule = ledgerManager()->sync(_groupID);
    if (syncModule->blockNumberFarBehind())
    {
        BOOST_THROW_EXCEPTION(
            TransactionRefused() << errinfo_comment("ImportResult::NodeIsSyncing"));
    }
}

std::string Rpc::getSystemConfigByKey(int _groupID, const std::string& key)
{
    try
    {
        RPC_LOG(INFO) << LOG_BADGE("getSystemConfigByKey") << LOG_DESC("request")
                      << LOG_KV("groupID", _groupID) << LOG_KV("key", key);

        checkRequest(_groupID);
        auto blockchain = ledgerManager()->blockChain(_groupID);
        if (!isValidSystemConfig(key))
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(RPCExceptionType::InvalidSystemConfig,
                RPCMsg[RPCExceptionType::InvalidSystemConfig]));
        }
        return blockchain->getSystemConfigByKey(key);
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, e.what()));
    }
}

std::string Rpc::getBlockNumber(int _groupID)
{
    try
    {
        RPC_LOG(DEBUG) << LOG_BADGE("getBlockNumber") << LOG_DESC("request")
                       << LOG_KV("groupID", _groupID);

        checkRequest(_groupID);
        auto blockchain = ledgerManager()->blockChain(_groupID);
        return toJS(blockchain->number());
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }
}


std::string Rpc::getPbftView(int _groupID)
{
    try
    {
        RPC_LOG(INFO) << LOG_BADGE("getPbftView") << LOG_DESC("request")
                      << LOG_KV("groupID", _groupID);

        checkRequest(_groupID);
        auto ledgerParam = ledgerManager()->getParamByGroupId(_groupID);
        auto consensusParam = ledgerParam->mutableConsensusParam();
        std::string consensusType = consensusParam.consensusType;
        if (consensusType != "pbft")
        {
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::NoView, RPCMsg[RPCExceptionType::NoView]));
        }
        auto consensus = ledgerManager()->consensus(_groupID);
        if (!consensus)
        {
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::GroupID, RPCMsg[RPCExceptionType::GroupID]));
        }
        dev::consensus::VIEWTYPE view = consensus->view();
        return toJS(view);
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }
}

Json::Value Rpc::getSealerList(int _groupID)
{
    try
    {
        RPC_LOG(INFO) << LOG_BADGE("getSealerList") << LOG_DESC("request")
                      << LOG_KV("groupID", _groupID);

        checkRequest(_groupID);
        auto blockchain = ledgerManager()->blockChain(_groupID);
        auto sealers = blockchain->sealerList();

        Json::Value response = Json::Value(Json::arrayValue);
        for (auto it = sealers.begin(); it != sealers.end(); ++it)
        {
            response.append((*it).hex());
        }

        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }
}

Json::Value Rpc::getObserverList(int _groupID)
{
    try
    {
        RPC_LOG(INFO) << LOG_BADGE("getObserverList") << LOG_DESC("request")
                      << LOG_KV("groupID", _groupID);

        checkRequest(_groupID);
        auto blockchain = ledgerManager()->blockChain(_groupID);
        auto observers = blockchain->observerList();

        Json::Value response = Json::Value(Json::arrayValue);
        for (auto it = observers.begin(); it != observers.end(); ++it)
        {
            response.append((*it).hex());
        }

        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }
}
Json::Value Rpc::getConsensusStatus(int _groupID)
{
    try
    {
        RPC_LOG(INFO) << LOG_BADGE("getConsensusStatus") << LOG_DESC("request")
                      << LOG_KV("groupID", _groupID);

        checkRequest(_groupID);
        auto consensus = ledgerManager()->consensus(_groupID);

        std::string status = consensus->consensusStatus();
        Json::Reader reader;
        Json::Value statusJson;
        if (!reader.parse(status, statusJson))
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::JsonParse, RPCMsg[RPCExceptionType::JsonParse]));

        return statusJson;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }
}

Json::Value Rpc::getSyncStatus(int _groupID)
{
    try
    {
        RPC_LOG(INFO) << LOG_BADGE("getSyncStatus") << LOG_DESC("request")
                      << LOG_KV("groupID", _groupID);

        checkRequest(_groupID);
        auto sync = ledgerManager()->sync(_groupID);

        std::string status = sync->syncInfo();
        Json::Reader reader;
        Json::Value statusJson;
        if (!reader.parse(status, statusJson))
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::JsonParse, RPCMsg[RPCExceptionType::JsonParse]));

        return statusJson;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }
}


Json::Value Rpc::getClientVersion()
{
    try
    {
        RPC_LOG(INFO) << LOG_BADGE("getClientVersion") << LOG_DESC("request");
        Json::Value version;

        version["FISCO-BCOS Version"] = g_BCOSConfig.binaryInfo.version;
        version["Supported Version"] = g_BCOSConfig.supportedVersion();
        version["Chain Id"] = toString(g_BCOSConfig.chainId());
        version["Build Time"] = g_BCOSConfig.binaryInfo.buildTime;
        version["Build Type"] = g_BCOSConfig.binaryInfo.buildInfo;
        version["Git Branch"] = g_BCOSConfig.binaryInfo.gitBranch;
        version["Git Commit Hash"] = g_BCOSConfig.binaryInfo.gitCommitHash;

        return version;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }

    return Json::Value();
}

Json::Value Rpc::getPeers(int)
{
    try
    {
        RPC_LOG(INFO) << LOG_BADGE("getPeers") << LOG_DESC("request");
        Json::Value response = Json::Value(Json::arrayValue);
        auto sessions = service()->sessionInfos();
        for (auto it = sessions.begin(); it != sessions.end(); ++it)
        {
            Json::Value node;
            node["NodeID"] = it->nodeInfo.nodeID.hex();
            node["IPAndPort"] = it->nodeIPEndpoint.name();
            node["Agency"] = it->nodeInfo.agencyName;
            node["Node"] = it->nodeInfo.nodeName;
            node["Topic"] = Json::Value(Json::arrayValue);
            for (auto topic : it->topics)
            {
                if (topic.topicStatus == dev::TopicStatus::VERIFYI_SUCCESS_STATUS)
                {
                    node["Topic"].append(topic.topic);
                }
            }
            response.append(node);
        }

        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }

    return Json::Value();
}

Json::Value Rpc::getNodeIDList(int)
{
    try
    {
        RPC_LOG(INFO) << LOG_BADGE("getNodeIDList") << LOG_DESC("request");

        Json::Value response = Json::Value(Json::arrayValue);

        response.append(service()->id().hex());
        auto sessions = service()->sessionInfos();
        for (auto it = sessions.begin(); it != sessions.end(); ++it)
        {
            response.append(it->nodeID().hex());
        }

        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }
}

Json::Value Rpc::getGroupPeers(int _groupID)
{
    try
    {
        RPC_LOG(INFO) << LOG_BADGE("getGroupPeers") << LOG_DESC("request")
                      << LOG_KV("groupID", _groupID);

        checkRequest(_groupID);
        Json::Value response = Json::Value(Json::arrayValue);

        auto _nodeList = service()->getNodeListByGroupID(_groupID);

        for (auto it = _nodeList.begin(); it != _nodeList.end(); ++it)
        {
            response.append((*it).hex());
        }
        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }

    return Json::Value();
}

Json::Value Rpc::getGroupList()
{
    try
    {
        RPC_LOG(INFO) << LOG_BADGE("getGroupList") << LOG_DESC("request");

        Json::Value response = Json::Value(Json::arrayValue);

        auto groupList = ledgerManager()->getGroupListForRpc();
        for (dev::GROUP_ID id : groupList)
            response.append(id);

        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }
}


Json::Value Rpc::getBlockByHash(
    int _groupID, const std::string& _blockHash, bool _includeTransactions)
{
    try
    {
        RPC_LOG(INFO) << LOG_BADGE("getBlockByHash") << LOG_DESC("request")
                      << LOG_KV("groupID", _groupID) << LOG_KV("blockHash", _blockHash)
                      << LOG_KV("includeTransaction", _includeTransactions);

        checkRequest(_groupID);
        Json::Value response;

        auto blockchain = ledgerManager()->blockChain(_groupID);

        h256 hash = jsToFixed<32>(_blockHash);
        auto block = blockchain->getBlockByHash(hash);
        if (!block)
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::BlockHash, RPCMsg[RPCExceptionType::BlockHash]));

        response["number"] = toJS(block->header().number());
        response["hash"] = _blockHash;
        response["parentHash"] = toJS(block->header().parentHash());
        response["logsBloom"] = toJS(block->header().logBloom());
        response["transactionsRoot"] = toJS(block->header().transactionsRoot());
        response["stateRoot"] = toJS(block->header().stateRoot());
        response["receiptsRoot"] = toJS(block->header().receiptsRoot());
        response["sealer"] = toJS(block->header().sealer());
        response["sealerList"] = Json::Value(Json::arrayValue);
        auto sealers = block->header().sealerList();
        for (auto it = sealers.begin(); it != sealers.end(); ++it)
        {
            response["sealerList"].append((*it).hex());
        }
        response["extraData"] = Json::Value(Json::arrayValue);
        auto datas = block->header().extraData();
        for (auto const& data : datas)
            response["extraData"].append(toJS(data));
        response["gasLimit"] = toJS(block->header().gasLimit());
        response["gasUsed"] = toJS(block->header().gasUsed());
        response["timestamp"] = toJS(block->header().timestamp());
        auto transactions = block->transactions();
        response["transactions"] = Json::Value(Json::arrayValue);
        for (unsigned i = 0; i < transactions->size(); i++)
        {
            if (_includeTransactions)
                response["transactions"].append(toJson(
                    *((*transactions)[i]), std::make_pair(hash, i), block->header().number()));
            else
                response["transactions"].append(toJS((*transactions)[i]->sha3()));
        }

        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }
}


Json::Value Rpc::getBlockByNumber(
    int _groupID, const std::string& _blockNumber, bool _includeTransactions)
{
    try
    {
        RPC_LOG(INFO) << LOG_BADGE("getBlockByNumber") << LOG_DESC("request")
                      << LOG_KV("groupID", _groupID) << LOG_KV("blockNumber", _blockNumber)
                      << LOG_KV("includeTransaction", _includeTransactions);

        checkRequest(_groupID);
        Json::Value response;

        BlockNumber number = jsToBlockNumber(_blockNumber);
        auto blockchain = ledgerManager()->blockChain(_groupID);

        auto block = blockchain->getBlockByNumber(number);
        if (!block)
            BOOST_THROW_EXCEPTION(JsonRpcException(
                RPCExceptionType::BlockNumberT, RPCMsg[RPCExceptionType::BlockNumberT]));

        response["number"] = toJS(number);
        response["hash"] = toJS(block->headerHash());
        response["parentHash"] = toJS(block->header().parentHash());
        response["logsBloom"] = toJS(block->header().logBloom());
        response["transactionsRoot"] = toJS(block->header().transactionsRoot());
        response["receiptsRoot"] = toJS(block->header().receiptsRoot());
        response["dbHash"] = toJS(block->header().dbHash());
        response["stateRoot"] = toJS(block->header().stateRoot());
        response["sealer"] = toJS(block->header().sealer());
        response["sealerList"] = Json::Value(Json::arrayValue);
        auto sealers = block->header().sealerList();
        for (auto it = sealers.begin(); it != sealers.end(); ++it)
        {
            response["sealerList"].append((*it).hex());
        }
        response["extraData"] = Json::Value(Json::arrayValue);
        auto datas = block->header().extraData();
        for (auto const& data : datas)
            response["extraData"].append(toJS(data));
        response["gasLimit"] = toJS(block->header().gasLimit());
        response["gasUsed"] = toJS(block->header().gasUsed());
        response["timestamp"] = toJS(block->header().timestamp());
        auto transactions = block->transactions();
        response["transactions"] = Json::Value(Json::arrayValue);
        for (unsigned i = 0; i < transactions->size(); i++)
        {
            if (_includeTransactions)
                response["transactions"].append(toJson(*((*transactions)[i]),
                    std::make_pair(block->headerHash(), i), block->header().number()));
            else
                response["transactions"].append(toJS((*transactions)[i]->sha3()));
        }

        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }
}

std::string Rpc::getBlockHashByNumber(int _groupID, const std::string& _blockNumber)
{
    try
    {
        RPC_LOG(INFO) << LOG_BADGE("getBlockHashByNumber") << LOG_DESC("request")
                      << LOG_KV("groupID", _groupID) << LOG_KV("blockNumber", _blockNumber);

        checkRequest(_groupID);
        auto blockchain = ledgerManager()->blockChain(_groupID);

        BlockNumber number = jsToBlockNumber(_blockNumber);
        h256 blockHash = blockchain->numberHash(number);
        // get blockHash failed
        if (blockHash == h256())
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(
                RPCExceptionType::BlockNumberT, RPCMsg[RPCExceptionType::BlockNumberT]));
        }
        return toJS(blockHash);
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }
}

Json::Value Rpc::getTransactionByHash(int _groupID, const std::string& _transactionHash)
{
    try
    {
        RPC_LOG(INFO) << LOG_BADGE("getTransactionByHash") << LOG_DESC("request")
                      << LOG_KV("groupID", _groupID) << LOG_KV("transactionHash", _transactionHash);

        checkRequest(_groupID);
        Json::Value response;
        auto blockchain = ledgerManager()->blockChain(_groupID);

        h256 hash = jsToFixed<32>(_transactionHash);
        auto tx = blockchain->getLocalisedTxByHash(hash);
        if (tx->blockNumber() == INVALIDNUMBER)
            return Json::nullValue;

        response["blockHash"] = toJS(tx->blockHash());
        response["blockNumber"] = toJS(tx->blockNumber());
        response["from"] = toJS(tx->from());
        response["gas"] = toJS(tx->gas());
        response["gasPrice"] = toJS(tx->gasPrice());
        response["hash"] = toJS(hash);
        response["input"] = toJS(tx->data());
        response["nonce"] = toJS(tx->nonce());
        response["to"] = toJS(tx->to());
        response["transactionIndex"] = toJS(tx->transactionIndex());
        response["value"] = toJS(tx->value());

        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }
}


Json::Value Rpc::getTransactionByBlockHashAndIndex(
    int _groupID, const std::string& _blockHash, const std::string& _transactionIndex)
{
    try
    {
        RPC_LOG(INFO) << LOG_BADGE("getTransactionByBlockHashAndIndex") << LOG_DESC("request")
                      << LOG_KV("groupID", _groupID) << LOG_KV("blockHash", _blockHash)
                      << LOG_KV("transactionIndex", _transactionIndex);

        checkRequest(_groupID);
        Json::Value response;

        auto blockchain = ledgerManager()->blockChain(_groupID);

        h256 hash = jsToFixed<32>(_blockHash);
        auto block = blockchain->getBlockByHash(hash);
        if (!block)
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::BlockHash, RPCMsg[RPCExceptionType::BlockHash]));

        auto transactions = block->transactions();
        unsigned int txIndex = jsToInt(_transactionIndex);
        if (txIndex >= transactions->size())
            BOOST_THROW_EXCEPTION(JsonRpcException(
                RPCExceptionType::TransactionIndex, RPCMsg[RPCExceptionType::TransactionIndex]));

        auto tx = (*transactions)[txIndex];
        response["blockHash"] = _blockHash;
        response["blockNumber"] = toJS(block->header().number());
        response["from"] = toJS(tx->from());
        response["gas"] = toJS(tx->gas());
        response["gasPrice"] = toJS(tx->gasPrice());
        response["hash"] = toJS(tx->sha3());
        response["input"] = toJS(tx->data());
        response["nonce"] = toJS(tx->nonce());
        response["to"] = toJS(tx->to());
        response["transactionIndex"] = toJS(txIndex);
        response["value"] = toJS(tx->value());

        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }
}


Json::Value Rpc::getTransactionByBlockNumberAndIndex(
    int _groupID, const std::string& _blockNumber, const std::string& _transactionIndex)
{
    try
    {
        RPC_LOG(INFO) << LOG_BADGE("getTransactionByBlockHashAndIndex") << LOG_DESC("request")
                      << LOG_KV("groupID", _groupID) << LOG_KV("blockNumber", _blockNumber)
                      << LOG_KV("transactionIndex", _transactionIndex);

        checkRequest(_groupID);
        Json::Value response;

        auto blockchain = ledgerManager()->blockChain(_groupID);

        BlockNumber number = jsToBlockNumber(_blockNumber);
        auto block = blockchain->getBlockByNumber(number);
        if (!block)
            BOOST_THROW_EXCEPTION(JsonRpcException(
                RPCExceptionType::BlockNumberT, RPCMsg[RPCExceptionType::BlockNumberT]));

        auto transactions = block->transactions();
        unsigned int txIndex = jsToInt(_transactionIndex);
        if (txIndex >= transactions->size())
            BOOST_THROW_EXCEPTION(JsonRpcException(
                RPCExceptionType::TransactionIndex, RPCMsg[RPCExceptionType::TransactionIndex]));

        auto tx = (*transactions)[txIndex];
        response["blockHash"] = toJS(block->header().hash());
        response["blockNumber"] = toJS(block->header().number());
        response["from"] = toJS(tx->from());
        response["gas"] = toJS(tx->gas());
        response["gasPrice"] = toJS(tx->gasPrice());
        response["hash"] = toJS(tx->sha3());
        response["input"] = toJS(tx->data());
        response["nonce"] = toJS(tx->nonce());
        response["to"] = toJS(tx->to());
        response["transactionIndex"] = toJS(txIndex);
        response["value"] = toJS(tx->value());

        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }
}


Json::Value Rpc::getTransactionReceipt(int _groupID, const std::string& _transactionHash)
{
    try
    {
        RPC_LOG(INFO) << LOG_BADGE("getTransactionByBlockHashAndIndex") << LOG_DESC("request")
                      << LOG_KV("groupID", _groupID) << LOG_KV("transactionHash", _transactionHash);

        checkRequest(_groupID);

        h256 hash = jsToFixed<32>(_transactionHash);
        auto blockchain = ledgerManager()->blockChain(_groupID);
        auto tx = blockchain->getLocalisedTxByHash(hash);
        if (tx->blockNumber() == INVALIDNUMBER)
            return Json::nullValue;
        auto txReceipt = blockchain->getLocalisedTxReceiptByHash(hash);
        if (txReceipt->blockNumber() == INVALIDNUMBER)
            return Json::nullValue;

        Json::Value response;
        response["transactionHash"] = _transactionHash;
        response["transactionIndex"] = toJS(txReceipt->transactionIndex());
        response["root"] = toJS(txReceipt->stateRoot());
        response["blockNumber"] = toJS(txReceipt->blockNumber());
        response["blockHash"] = toJS(txReceipt->blockHash());
        response["from"] = toJS(txReceipt->from());
        response["to"] = toJS(txReceipt->to());
        response["gasUsed"] = toJS(txReceipt->gasUsed());
        response["contractAddress"] = toJS(txReceipt->contractAddress());
        response["logs"] = Json::Value(Json::arrayValue);
        for (unsigned int i = 0; i < txReceipt->log().size(); ++i)
        {
            Json::Value log;
            log["address"] = toJS(txReceipt->log()[i].address);
            log["topics"] = Json::Value(Json::arrayValue);
            for (unsigned int j = 0; j < txReceipt->log()[i].topics.size(); ++j)
                log["topics"].append(toJS(txReceipt->log()[i].topics[j]));
            log["data"] = toJS(txReceipt->log()[i].data);
            response["logs"].append(log);
        }
        response["logsBloom"] = toJS(txReceipt->bloom());
        response["status"] = toJS(txReceipt->status());
        response["input"] = toJS(tx->data());
        response["output"] = toJS(txReceipt->outputBytes());

        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }
}


Json::Value Rpc::getPendingTransactions(int _groupID)
{
    try
    {
        RPC_LOG(INFO) << LOG_BADGE("getPendingTransactions") << LOG_DESC("request")
                      << LOG_KV("groupID", _groupID);

        checkRequest(_groupID);
        Json::Value response;

        auto txPool = ledgerManager()->txPool(_groupID);

        response = Json::Value(Json::arrayValue);
        auto transactions = txPool->pendingList();
        for (size_t i = 0; i < transactions->size(); ++i)
        {
            auto tx = (*transactions)[i];
            Json::Value txJson;
            txJson["from"] = toJS(tx->from());
            txJson["gas"] = toJS(tx->gas());
            txJson["gasPrice"] = toJS(tx->gasPrice());
            txJson["hash"] = toJS(tx->sha3());
            txJson["input"] = toJS(tx->data());
            txJson["nonce"] = toJS(tx->nonce());
            txJson["to"] = toJS(tx->to());
            txJson["value"] = toJS(tx->value());
            response.append(txJson);
        }

        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }
}

std::string Rpc::getPendingTxSize(int _groupID)
{
    try
    {
        RPC_LOG(INFO) << LOG_BADGE("getPendingTxSize") << LOG_DESC("request")
                      << LOG_KV("groupID", _groupID);

        checkRequest(_groupID);
        auto txPool = ledgerManager()->txPool(_groupID);

        return toJS(txPool->status().current);
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }
}

std::string Rpc::getCode(int _groupID, const std::string& _address)
{
    try
    {
        RPC_LOG(INFO) << LOG_BADGE("getCode") << LOG_DESC("request") << LOG_KV("groupID", _groupID)
                      << LOG_KV("address", _address);

        checkRequest(_groupID);
        auto blockChain = ledgerManager()->blockChain(_groupID);

        return toJS(blockChain->getCode(jsToAddress(_address)));
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }
}

Json::Value Rpc::getTotalTransactionCount(int _groupID)
{
    try
    {
        checkRequest(_groupID);
        auto blockChain = ledgerManager()->blockChain(_groupID);

        Json::Value response;
        std::pair<int64_t, int64_t> result = blockChain->totalTransactionCount();
        response["txSum"] = toJS(result.first);
        response["blockNumber"] = toJS(result.second);
        result = blockChain->totalFailedTransactionCount();
        response["failedTxSum"] = toJS(result.first);
        return response;
#if 0
        RPC_LOG(INFO) << LOG_BADGE("getTotalTransactionCount") << LOG_DESC("request")
                      << LOG_KV("groupID", _groupID);

        checkRequest(_groupID);
        auto blockChain = ledgerManager()->blockChain(_groupID);

        std::pair<int64_t, int64_t> result = blockChain->totalTransactionCount();

        Json::Value response;
        auto txPool = ledgerManager()->txPool(_groupID);

        auto ret = txPool->totalTransactionNum();
        response["txSum"] = toJS(ret);
        response["blockNumber"] = toJS(result.second);
        result = blockChain->totalFailedTransactionCount();
        response["failedTxSum"] = toJS(result.first);
#endif
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }
}

Json::Value Rpc::call(int _groupID, const Json::Value& request)
{
    try
    {
        RPC_LOG(TRACE) << LOG_BADGE("call") << LOG_DESC("request") << LOG_KV("groupID", _groupID)
                       << LOG_KV("callParams", request.toStyledString());

        checkRequest(_groupID);
        if (request["from"].empty() || request["from"].asString().empty())
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::CallFrom, RPCMsg[RPCExceptionType::CallFrom]));

        auto blockchain = ledgerManager()->blockChain(_groupID);
        auto blockverfier = ledgerManager()->blockVerifier(_groupID);

        BlockNumber blockNumber = blockchain->number();
        auto block = blockchain->getBlockByNumber(blockNumber);
        if (!block)
            BOOST_THROW_EXCEPTION(JsonRpcException(
                RPCExceptionType::BlockNumberT, RPCMsg[RPCExceptionType::BlockNumberT]));

        TransactionSkeleton txSkeleton = toTransactionSkeleton(request);
        Transaction::Ptr tx = std::make_shared<Transaction>(txSkeleton.value, gasPrice,
            maxTransactionGasLimit, txSkeleton.to, txSkeleton.data, txSkeleton.nonce);
        auto blockHeader = block->header();
        tx->forceSender(txSkeleton.from);
        auto executionResult = blockverfier->executeTransaction(blockHeader, tx);

        Json::Value response;
        response["currentBlockNumber"] = toJS(blockNumber);
        response["status"] = toJS(executionResult->status());
        response["output"] = toJS(executionResult->outputBytes());
        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }
}

std::string Rpc::sendRawTransaction(int _groupID, const std::string& _rlp)
{
    try
    {
#if 0
        RPC_LOG(TRACE) << LOG_BADGE("sendRawTransaction") << LOG_DESC("request")
                       << LOG_KV("groupID", _groupID) << LOG_KV("rlp", _rlp);
#endif
        auto txPool = ledgerManager()->txPool(_groupID);
        // only check txPool here
        if (!txPool)
        {
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::GroupID, RPCMsg[RPCExceptionType::GroupID]));
        }

        // Transaction tx(jsToBytes(_rlp, OnFailed::Throw), CheckTransaction::Everything);
        Transaction::Ptr tx = std::make_shared<Transaction>(
            jsToBytes(_rlp, OnFailed::Throw), CheckTransaction::Everything);
        auto currentTransactionCallback = m_currentTransactionCallback.get();

        uint32_t clientProtocolversion = ProtocolVersion::v1;
        if (currentTransactionCallback)
        {
            auto transactionCallback = *currentTransactionCallback;
            clientProtocolversion = (*m_transactionCallbackVersion)();
            tx->setRpcCallback(
                [transactionCallback, clientProtocolversion](
                    LocalisedTransactionReceipt::Ptr receipt, dev::bytesConstRef input) {
                    Json::Value response;
                    if (clientProtocolversion > 0)
                    {  // FIXME: If made protocol modify, please modify upside if
                        response["transactionHash"] = toJS(receipt->hash());
                        response["transactionIndex"] = toJS(receipt->transactionIndex());
                        response["root"] = toJS(receipt->stateRoot());
                        response["blockNumber"] = toJS(receipt->blockNumber());
                        response["blockHash"] = toJS(receipt->blockHash());
                        response["from"] = toJS(receipt->from());
                        response["to"] = toJS(receipt->to());
                        response["gasUsed"] = toJS(receipt->gasUsed());
                        response["contractAddress"] = toJS(receipt->contractAddress());
                        response["logs"] = Json::Value(Json::arrayValue);
                        for (unsigned int i = 0; i < receipt->log().size(); ++i)
                        {
                            Json::Value log;
                            log["address"] = toJS(receipt->log()[i].address);
                            log["topics"] = Json::Value(Json::arrayValue);
                            for (unsigned int j = 0; j < receipt->log()[i].topics.size(); ++j)
                                log["topics"].append(toJS(receipt->log()[i].topics[j]));
                            log["data"] = toJS(receipt->log()[i].data);
                            response["logs"].append(log);
                        }
                        response["logsBloom"] = toJS(receipt->bloom());
                        response["status"] = toJS(receipt->status());
                        if (g_BCOSConfig.version() > RC3_VERSION)
                        {
                            response["input"] = toJS(input);
                        }
                        response["output"] = toJS(receipt->outputBytes());
                    }

                    auto receiptContent = response.toStyledString();
                    transactionCallback(receiptContent);
                });
        }
        // calculate the sha3 before submit into the transaction pool
        tx->sha3();
        std::pair<h256, Address> ret;
        switch (clientProtocolversion)
        {
        // the oldest SDK: submit transactions sync
        case ProtocolVersion::v1:
        case ProtocolVersion::v2:
            checkRequest(_groupID);
            checkSyncStatus(_groupID);
            ret = txPool->submitTransactions(tx);
            break;
        // TODO: the SDK protocol upgrade to 3,
        // the v2 submit transactions sync
        // and v3 submit transactions async
        case ProtocolVersion::v3:
            ret = txPool->submit(tx);
            break;
        // default submit transactions sync
        default:
            checkRequest(_groupID);
            checkSyncStatus(_groupID);
            ret = txPool->submitTransactions(tx);
            break;
        }
        return toJS(ret.first);
    }
    catch (JsonRpcException& e)
    {
        RPC_LOG(WARNING) << LOG_BADGE("sendRawTransaction") << LOG_DESC("response")
                         << LOG_KV("groupID", _groupID) << LOG_KV("errorCode", e.GetCode())
                         << LOG_KV("errorMessage", e.GetMessage());
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }
}

// Get transaction with merkle proof by hash
Json::Value Rpc::getTransactionByHashWithProof(int _groupID, const std::string& _transactionHash)
{
    try
    {
        if (g_BCOSConfig.version() < V2_2_0)
        {
            RPC_LOG(ERROR) << "getTransactionByHashWithProof only support after by v2.2.0";
            BOOST_THROW_EXCEPTION(JsonRpcException(RPCExceptionType::InvalidRequest,
                "method getTransactionByHashWithProof not support this version"));
        }
        RPC_LOG(INFO) << LOG_BADGE("getTransactionByHashWithProof") << LOG_DESC("request")
                      << LOG_KV("groupID", _groupID) << LOG_KV("transactionHash", _transactionHash);
        checkRequest(_groupID);
        Json::Value response;
        auto blockchain = ledgerManager()->blockChain(_groupID);
        h256 hash = jsToFixed<32>(_transactionHash);
        auto tx = blockchain->getTransactionByHashWithProof(hash);

        auto transaction = tx.first;
        if (transaction->blockNumber() == INVALIDNUMBER)
        {
            return Json::nullValue;
        }
        response["transaction"]["blockHash"] = toJS(transaction->blockHash());
        response["transaction"]["blockNumber"] = toJS(transaction->blockNumber());
        response["transaction"]["from"] = toJS(transaction->from());
        response["transaction"]["gas"] = toJS(transaction->gas());
        response["transaction"]["gasPrice"] = toJS(transaction->gasPrice());
        response["transaction"]["hash"] = toJS(hash);
        response["transaction"]["input"] = toJS(transaction->data());
        response["transaction"]["nonce"] = toJS(transaction->nonce());
        response["transaction"]["to"] = toJS(transaction->to());
        response["transaction"]["transactionIndex"] = toJS(transaction->transactionIndex());
        response["transaction"]["value"] = toJS(transaction->value());


        const auto& merkleList = tx.second;
        uint32_t index = 0;
        for (const auto& merkleItem : merkleList)
        {
            response["txProof"][index]["left"] = Json::arrayValue;
            response["txProof"][index]["right"] = Json::arrayValue;
            const auto& left = merkleItem.first;
            for (const auto& item : left)
            {
                response["txProof"][index]["left"].append(item);
            }

            const auto& right = merkleItem.second;
            for (const auto& item : right)
            {
                response["txProof"][index]["right"].append(item);
            }
            ++index;
        }
        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }
}
// Get receipt with merkle proof by hash
Json::Value Rpc::getTransactionReceiptByHashWithProof(
    int _groupID, const std::string& _transactionHash)
{
    try
    {
        RPC_LOG(INFO) << LOG_BADGE("getTransactionReceiptByHashWithProof") << LOG_DESC("request")
                      << LOG_KV("groupID", _groupID) << LOG_KV("transactionHash", _transactionHash);

        if (g_BCOSConfig.version() < V2_2_0)
        {
            RPC_LOG(ERROR) << "getTransactionReceiptByHashWithProof only support after by v2.2.0";
            BOOST_THROW_EXCEPTION(JsonRpcException(RPCExceptionType::InvalidRequest,
                "method getTransactionReceiptByHashWithProof not support this version"));
        }
        checkRequest(_groupID);
        h256 hash = jsToFixed<32>(_transactionHash);
        auto blockchain = ledgerManager()->blockChain(_groupID);
        dev::eth::LocalisedTransaction transaction;
        auto receipt = blockchain->getTransactionReceiptByHashWithProof(hash, transaction);
        auto txReceipt = receipt.first;
        if (txReceipt->blockNumber() == INVALIDNUMBER || transaction.blockNumber() == INVALIDNUMBER)
            return Json::nullValue;

        Json::Value response;
        response["transactionReceipt"]["transactionHash"] = _transactionHash;
        response["transactionReceipt"]["root"] = toJS(txReceipt->stateRoot());
        response["transactionReceipt"]["transactionIndex"] = toJS(txReceipt->transactionIndex());
        response["transactionReceipt"]["blockNumber"] = toJS(txReceipt->blockNumber());
        response["transactionReceipt"]["blockHash"] = toJS(txReceipt->blockHash());
        response["transactionReceipt"]["from"] = toJS(txReceipt->from());
        response["transactionReceipt"]["to"] = toJS(txReceipt->to());
        response["transactionReceipt"]["gasUsed"] = toJS(txReceipt->gasUsed());
        response["transactionReceipt"]["contractAddress"] = toJS(txReceipt->contractAddress());
        response["transactionReceipt"]["logs"] = Json::Value(Json::arrayValue);
        for (unsigned int i = 0; i < txReceipt->log().size(); ++i)
        {
            Json::Value log;
            log["address"] = toJS(txReceipt->log()[i].address);
            log["topics"] = Json::Value(Json::arrayValue);
            for (unsigned int j = 0; j < txReceipt->log()[i].topics.size(); ++j)
                log["topics"].append(toJS(txReceipt->log()[i].topics[j]));
            log["data"] = toJS(txReceipt->log()[i].data);
            response["transactionReceipt"]["logs"].append(log);
        }
        response["transactionReceipt"]["logsBloom"] = toJS(txReceipt->bloom());
        response["transactionReceipt"]["status"] = toJS(txReceipt->status());
        response["transactionReceipt"]["input"] = toJS(transaction.data());
        response["transactionReceipt"]["output"] = toJS(txReceipt->outputBytes());


        const auto& merkleList = receipt.second;
        uint32_t index = 0;
        for (const auto& merkleItem : merkleList)
        {
            response["receiptProof"][index]["left"] = Json::arrayValue;
            response["receiptProof"][index]["right"] = Json::arrayValue;
            const auto& left = merkleItem.first;
            for (const auto& item : left)
            {
                response["receiptProof"][index]["left"].append(item);
            }

            const auto& right = merkleItem.second;
            for (const auto& item : right)
            {
                response["receiptProof"][index]["right"].append(item);
            }
            ++index;
        }

        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }
}


Json::Value Rpc::generateGroup(
    int _groupID, const std::string& _timestamp, const std::set<std::string>& _sealerList)
{
    RPC_LOG(INFO) << LOG_BADGE("generateGroup") << LOG_DESC("query generateGroup")
                  << LOG_KV("groupID", _groupID) << LOG_KV("timestamp", _timestamp)
                  << LOG_KV("sealerList.size", _sealerList.size());

    if (g_BCOSConfig.version() < V2_2_0)
    {
        RPC_LOG(ERROR) << "generateGroup only support after by v2.2.0";
        BOOST_THROW_EXCEPTION(JsonRpcException(
            RPCExceptionType::InvalidRequest, "method generateGroup not support this version"));
    }

    Json::Value response;
    try
    {
        if (!GroupGenerator::checkGroupID(_groupID))
        {
            BOOST_THROW_EXCEPTION(InvalidGenesisGroupID());
        }

        if (ledgerManager()->isLedgerExist(GROUP_ID(_groupID)))
        {
            BOOST_THROW_EXCEPTION(GroupExists());
        }

        GroupGenerator::generate(GROUP_ID(_groupID), _timestamp, _sealerList);

        response["code"] = GroupGeneratorCode::SUCCESS;
        response["message"] = "success";
    }
    catch (GroupExists const& _e)
    {
        response["code"] = GroupGeneratorCode::GROUP_EXIST;
        response["message"] = "Group " + std::to_string(_groupID) + " is running";
    }
    catch (GenesisExists const& _e)
    {
        response["code"] = GroupGeneratorCode::GROUP_GENESIS_FILE_EXIST;
        response["message"] = "Group " + std::to_string(_groupID) + " genesis exists";
    }
    catch (GroupConfigExists const& _e)
    {
        response["code"] = GroupGeneratorCode::GROUP_CONFIG_FILE_EXIST;
        response["message"] = "Group " + std::to_string(_groupID) + " config exists";
    }
    catch (GroupGeneratorException const& _e)
    {
        response["code"] = GroupGeneratorCode::INVALID_PARAMS;
        response["message"] = "Group generator error: GroupID: " + std::to_string(_groupID) +
                              " timestamp: " + _timestamp +
                              " sealerlist.size: " + std::to_string(_sealerList.size());
    }
    catch (InvalidGenesisGroupID const& _e)
    {
        response["code"] = GroupStarterCode::INVALID_PARAMS;
        response["message"] = "GroupID out of bound: " + std::to_string(_groupID);
    }
    catch (InvalidGenesisTimestamp const& _e)
    {
        response["code"] = GroupStarterCode::INVALID_PARAMS;
        response["message"] = "Invalid timestamp: " + _timestamp;
    }
    catch (InvalidGenesisNodeid const& _e)
    {
        response["code"] = GroupStarterCode::INVALID_PARAMS;

        std::string nodeIDs;
        for (auto nodeid : _sealerList)
        {
            nodeIDs += nodeid + ",";
        }
        response["message"] = "Invalid Nodeids: " + nodeIDs;
    }
    catch (std::exception const& _e)
    {
        response["code"] = GroupGeneratorCode::OTHER_ERROR;
        response["message"] = "Internal error";
    }
    return response;
}


Json::Value Rpc::startGroup(int _groupID)
{
    RPC_LOG(INFO) << LOG_BADGE("startGroup") << LOG_DESC("query startGroup")
                  << LOG_KV("groupID", _groupID);

    if (g_BCOSConfig.version() < V2_2_0)
    {
        RPC_LOG(ERROR) << "startGroup only support after by v2.2.0";
        BOOST_THROW_EXCEPTION(JsonRpcException(
            RPCExceptionType::InvalidRequest, "method startGroup not support this version"));
    }

    Json::Value response;
    try
    {
        if (!GroupGenerator::checkGroupID(_groupID))
        {
            BOOST_THROW_EXCEPTION(InvalidGenesisGroupID());
        }

        bool success = m_ledgerInitializer->initLedgerByGroupID(GROUP_ID(_groupID));
        if (!success)
        {
            throw new dev::Exception("Init ledger failed");
        }

        success = m_ledgerManager->startByGroupID(GROUP_ID(_groupID));
        if (!success)
        {
            m_ledgerManager->removeLedger(GROUP_ID(_groupID));
            throw new dev::Exception("start ledger failed");
        }

        response["code"] = GroupStarterCode::SUCCESS;
        response["message"] = "success";
    }
    catch (GroupExists const& _e)
    {
        response["code"] = GroupStarterCode::GROUP_IS_RUNNING;
        response["message"] = "Group " + std::to_string(_groupID) + " has been loaded";
    }
    catch (GenesisNotExists const& _e)
    {
        response["code"] = GroupStarterCode::GROUP_GENESIS_FILE_NOT_EXIST;
        response["message"] = "Group " + std::to_string(_groupID) + " genesis not exists";
    }
    catch (GroupConfigNotExists const& _e)
    {
        response["code"] = GroupStarterCode::GROUP_CONFIG_FILE_NOT_EXIST;
        response["message"] = "success" + std::to_string(_groupID) + " group config not exists";
    }
    catch (GroupGeneratorException const& _e)
    {
        response["code"] = GroupStarterCode::INVALID_PARAMS;
        response["message"] = "Group start error: GroupID: " + std::to_string(_groupID);
    }
    catch (InvalidGenesisGroupID const& _e)
    {
        response["code"] = GroupStarterCode::INVALID_PARAMS;
        response["message"] = "GroupID out of bound: " + std::to_string(_groupID);
    }
    catch (std::exception const& _e)
    {
        response["code"] = GroupStarterCode::OTHER_ERROR;
        response["message"] = _e.what();
    }
    return response;
}
