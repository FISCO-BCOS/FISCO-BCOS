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
#include "JsonHelper.h"
#include <jsonrpccpp/common/exception.h>
#include <jsonrpccpp/server.h>
#include <libconfig/GlobalConfigure.h>
#include <libdevcore/CommonData.h>
#include <libethcore/Common.h>
#include <libethcore/CommonJS.h>
#include <libethcore/Transaction.h>
#include <libprecompiled/SystemConfigPrecompiled.h>
#include <libsync/SyncStatus.h>
#include <libtxpool/TxPoolInterface.h>
#include <boost/algorithm/hex.hpp>
#include <csignal>
#include <sstream>

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
    {RPCExceptionType::IncompleteInitialization, "RPC module initialization is incomplete"},
    {RPCExceptionType::OverQPSLimit, "Over QPS limit"},
    {RPCExceptionType::PermissionDenied, "The SDK is not allowed to access this group"}};

Rpc::Rpc(
    LedgerInitializer::Ptr _ledgerInitializer, std::shared_ptr<dev::p2p::P2PInterface> _service)
  : m_service(_service)
{
    setLedgerInitializer(_ledgerInitializer);
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

bool Rpc::isValidSystemConfig(std::string const& _key)
{
    return (c_supportedSystemConfigKeys.count(_key));
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
    checkLedgerStatus(syncModule, "sync", "checkSyncStatus");

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

        auto blockchain = ledgerManager()->blockChain(_groupID);
        checkLedgerStatus(blockchain, "BlockChain", "getSystemConfigByKey");

        checkRequest(_groupID);

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

        auto blockchain = ledgerManager()->blockChain(_groupID);
        checkLedgerStatus(blockchain, "BlockChain", "getBlockNumber");

        checkRequest(_groupID);
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

        auto consensus = ledgerManager()->consensus(_groupID);
        checkLedgerStatus(consensus, "consensus", "getPbftView");

        checkRequest(_groupID);
        auto ledgerParam = ledgerManager()->getParamByGroupId(_groupID);
        auto consensusParam = ledgerParam->mutableConsensusParam();
        std::string consensusType = consensusParam.consensusType;
        if (stringCmpIgnoreCase(consensusType, "pbft") != 0 &&
            stringCmpIgnoreCase(consensusType, "rpbft") != 0)
        {
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::NoView, RPCMsg[RPCExceptionType::NoView]));
        }
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

        auto blockchain = ledgerManager()->blockChain(_groupID);
        checkLedgerStatus(blockchain, "blockchain", "getSealerList");

        checkRequest(_groupID);
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

Json::Value Rpc::getEpochSealersList(int _groupID)
{
    try
    {
        auto ledgerParam = ledgerManager()->getParamByGroupId(_groupID);
        checkLedgerStatus(ledgerParam, "ledgerParam", "getEpochSealersList");
        checkRequest(_groupID);

        auto consensusType = ledgerParam->mutableConsensusParam().consensusType;
        if (stringCmpIgnoreCase(consensusType, "rpbft") != 0)
        {
            RPC_LOG(ERROR) << LOG_DESC("Only support getEpochSealersList when rpbft is used")
                           << LOG_KV("consensusType", consensusType) << LOG_KV("groupID", _groupID);

            BOOST_THROW_EXCEPTION(JsonRpcException(RPCExceptionType::InvalidRequest,
                "method getEpochSealersList only supported when rpbft is used, current "
                "consensus "
                "type is " +
                    consensusType));
        }
        RPC_LOG(INFO) << LOG_BADGE("getEpochSealersList") << LOG_DESC("request")
                      << LOG_KV("groupID", _groupID);

        auto consensus = ledgerManager()->consensus(_groupID);
        checkLedgerStatus(consensus, "consensus", "getEpochSealersList");
        checkRequest(_groupID);

        // get the chosed sealer list
        auto sealers = consensus->consensusList();
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

        auto blockchain = ledgerManager()->blockChain(_groupID);
        checkLedgerStatus(blockchain, "blockchain", "getObserverList");
        checkRequest(_groupID);

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

        auto consensus = ledgerManager()->consensus(_groupID);
        checkLedgerStatus(consensus, "consensus", "getConsensusStatus");

        checkRequest(_groupID);

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

        auto sync = ledgerManager()->sync(_groupID);
        checkLedgerStatus(sync, "sync", "getSyncStatus");

        checkRequest(_groupID);

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
            node["IPAndPort"] = boost::lexical_cast<std::string>(it->nodeIPEndpoint);
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

        auto blockchain = ledgerManager()->blockChain(_groupID);
        checkLedgerStatus(blockchain, "blockchain", "getBlockByHash");
        checkRequest(_groupID);
        Json::Value response;

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


void Rpc::generateBlockHeaderInfo(Json::Value& _response,
    std::shared_ptr<
        std::pair<std::shared_ptr<dev::eth::BlockHeader>, dev::eth::Block::SigListPtrType>>
        _headerInfo,
    bool _includeSigList)
{
    auto blockHeader = _headerInfo->first;
    _response["number"] = blockHeader->number();

    _response["hash"] = toJS(blockHeader->hash());
    _response["parentHash"] = toJS(blockHeader->parentHash());
    _response["logsBloom"] = toJS(blockHeader->logBloom());
    _response["transactionsRoot"] = toJS(blockHeader->transactionsRoot());
    _response["receiptsRoot"] = toJS(blockHeader->receiptsRoot());
    _response["dbHash"] = toJS(blockHeader->dbHash());
    _response["stateRoot"] = toJS(blockHeader->stateRoot());
    _response["sealer"] = toJS(blockHeader->sealer());
    _response["sealerList"] = Json::Value(Json::arrayValue);
    auto const sealerList = blockHeader->sealerList();
    for (auto it = sealerList.begin(); it != sealerList.end(); ++it)
    {
        _response["sealerList"].append((*it).hex());
    }
    _response["extraData"] = Json::Value(Json::arrayValue);
    auto datas = blockHeader->extraData();
    for (auto const& data : datas)
    {
        _response["extraData"].append(toJS(data));
    }
    _response["gasLimit"] = toJS(blockHeader->gasLimit());
    _response["gasUsed"] = toJS(blockHeader->gasUsed());
    _response["timestamp"] = toJS(blockHeader->timestamp());
    if (!_includeSigList)
    {
        return;
    }
    // signature list
    _response["signatureList"] = Json::Value(Json::arrayValue);
    auto sigList = _headerInfo->second;
    if (!sigList)
    {
        return;
    }
    for (auto const& signature : *sigList)
    {
        Json::Value sigJsonObj;
        sigJsonObj["index"] = toJS(signature.first);
        sigJsonObj["signature"] = toJS(signature.second);
        _response["signatureList"].append(sigJsonObj);
    }
}

Json::Value Rpc::getBlockHeaderByNumber(
    int _groupID, const std::string& _blockNumber, bool _includeSigList)
{
    try
    {
        RPC_LOG(INFO) << LOG_BADGE("getBlockHeaderByNumber") << LOG_DESC("request")
                      << LOG_KV("groupID", _groupID) << LOG_KV("blockNumber", _blockNumber);
        auto blockChain = ledgerManager()->blockChain(_groupID);
        checkLedgerStatus(blockChain, "blockchain", "getBlockHeaderByNumber");
        checkRequest(_groupID);

        BlockNumber number = jsToBlockNumber(_blockNumber);
        auto blockHeaderInfo = blockChain->getBlockHeaderInfo(number);
        if (!blockHeaderInfo)
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(
                RPCExceptionType::BlockNumberT, RPCMsg[RPCExceptionType::BlockNumberT]));
        }
        Json::Value response;
        generateBlockHeaderInfo(response, blockHeaderInfo, _includeSigList);
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

Json::Value Rpc::getBlockHeaderByHash(
    int _groupID, const std::string& _blockHash, bool _includeSigList)
{
    try
    {
        RPC_LOG(INFO) << LOG_BADGE("getBlockHeaderByHash") << LOG_DESC("request")
                      << LOG_KV("groupID", _groupID);
        auto blockChain = ledgerManager()->blockChain(_groupID);
        checkLedgerStatus(blockChain, "blockchain", "getBlockHeaderByHash");
        checkRequest(_groupID);
        h256 blockHash = jsToFixed<32>(_blockHash);
        auto blockHeaderInfo = blockChain->getBlockHeaderInfoByHash(blockHash);
        if (!blockHeaderInfo)
        {
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::BlockHash, RPCMsg[RPCExceptionType::BlockHash]));
        }
        Json::Value response;
        generateBlockHeaderInfo(response, blockHeaderInfo, _includeSigList);
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

        auto blockchain = ledgerManager()->blockChain(_groupID);
        checkLedgerStatus(blockchain, "blockchain", "getBlockByNumber");
        checkRequest(_groupID);
        Json::Value response;

        BlockNumber number = jsToBlockNumber(_blockNumber);

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

        auto blockchain = ledgerManager()->blockChain(_groupID);
        checkLedgerStatus(blockchain, "blockchain", "getBlockHashByNumber");
        checkRequest(_groupID);

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

        auto blockchain = ledgerManager()->blockChain(_groupID);
        checkLedgerStatus(blockchain, "blockchain", "getTransactionByHash");
        checkRequest(_groupID);
        Json::Value response;

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

        auto blockchain = ledgerManager()->blockChain(_groupID);
        checkLedgerStatus(blockchain, "blockchain", "getTransactionByBlockHashAndIndex");

        checkRequest(_groupID);
        Json::Value response;

        h256 hash = jsToFixed<32>(_blockHash);
        auto block = blockchain->getBlockByHash(hash);
        if (!block)
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::BlockHash, RPCMsg[RPCExceptionType::BlockHash]));

        auto transactions = block->transactions();
        int64_t txIndex = jsToInt(_transactionIndex);
        if (txIndex >= int64_t(transactions->size()))
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

        auto blockchain = ledgerManager()->blockChain(_groupID);
        checkLedgerStatus(blockchain, "blockchain", "getTransactionByBlockNumberAndIndex");
        checkRequest(_groupID);
        Json::Value response;


        BlockNumber number = jsToBlockNumber(_blockNumber);
        auto block = blockchain->getBlockByNumber(number);
        if (!block)
            BOOST_THROW_EXCEPTION(JsonRpcException(
                RPCExceptionType::BlockNumberT, RPCMsg[RPCExceptionType::BlockNumberT]));

        auto transactions = block->transactions();
        int64_t txIndex = jsToInt(_transactionIndex);
        if (txIndex >= int64_t(transactions->size()))
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

        auto blockchain = ledgerManager()->blockChain(_groupID);
        checkLedgerStatus(blockchain, "blockchain", "getTransactionReceipt");
        checkRequest(_groupID);

        h256 hash = jsToFixed<32>(_transactionHash);

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
        Json::Value response;

        auto txPool = ledgerManager()->txPool(_groupID);
        checkLedgerStatus(txPool, "txPool", "getPendingTransactions");

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
        auto txPool = ledgerManager()->txPool(_groupID);
        checkLedgerStatus(txPool, "txPool", "getPendingTxSize");

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

        auto blockChain = ledgerManager()->blockChain(_groupID);
        checkLedgerStatus(blockChain, "blockChain", "getCode");
        checkRequest(_groupID);

        return toJS(blockChain->getCode(jsToAddress(_address)));
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        RPC_LOG(ERROR) << LOG_DESC("getCode exceptioned") << LOG_KV("groupID", _groupID)
                       << LOG_KV("errorMessage", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }
}

Json::Value Rpc::getTotalTransactionCount(int _groupID)
{
    try
    {
        auto blockChain = ledgerManager()->blockChain(_groupID);
        checkLedgerStatus(blockChain, "blockChain", "getTotalTransactionCount");
        checkRequest(_groupID);

        Json::Value response;
        std::pair<int64_t, int64_t> result = blockChain->totalTransactionCount();
        response["txSum"] = toJS(result.first);
        response["blockNumber"] = toJS(result.second);
        result = blockChain->totalFailedTransactionCount();
        response["failedTxSum"] = toJS(result.first);
        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        RPC_LOG(ERROR) << LOG_DESC("getTotalTransactionCount exceptioned")
                       << LOG_KV("groupID", _groupID)
                       << LOG_KV("errorMessage", boost::diagnostic_information(e));
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

        auto blockchain = ledgerManager()->blockChain(_groupID);
        checkLedgerStatus(blockchain, "blockChain", "call");
        auto blockverfier = ledgerManager()->blockVerifier(_groupID);
        checkLedgerStatus(blockverfier, "blockverfier", "call");

        checkRequest(_groupID);
        if (request["from"].empty() || request["from"].asString().empty())
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::CallFrom, RPCMsg[RPCExceptionType::CallFrom]));

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
        auto transactionReceipt = blockverfier->executeTransaction(blockHeader, tx);

        Json::Value response;
        response["currentBlockNumber"] = toJS(blockNumber);
        response["status"] = toJS(transactionReceipt->status());
        response["output"] = toJS(transactionReceipt->outputBytes());
        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (std::exception& e)
    {
        RPC_LOG(ERROR) << LOG_DESC("call exceptioned") << LOG_KV("groupID", _groupID)
                       << LOG_KV("errorMessage", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(
            JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR, boost::diagnostic_information(e)));
    }
}


std::shared_ptr<Json::Value> Rpc::notifyReceipt(std::weak_ptr<dev::blockchain::BlockChainInterface>,
    LocalisedTransactionReceipt::Ptr receipt, dev::bytesConstRef input, dev::eth::Block::Ptr)
{
    std::shared_ptr<Json::Value> response = std::make_shared<Json::Value>();

    // FIXME: If made protocol modify, please modify upside if
    (*response)["transactionHash"] = toJS(receipt->hash());
    (*response)["transactionIndex"] = toJS(receipt->transactionIndex());
    (*response)["root"] = toJS(receipt->stateRoot());
    (*response)["blockNumber"] = toJS(receipt->blockNumber());
    (*response)["blockHash"] = toJS(receipt->blockHash());
    (*response)["from"] = toJS(receipt->from());
    (*response)["to"] = toJS(receipt->to());
    (*response)["gasUsed"] = toJS(receipt->gasUsed());
    (*response)["contractAddress"] = toJS(receipt->contractAddress());
    (*response)["logs"] = Json::Value(Json::arrayValue);
    for (unsigned int i = 0; i < receipt->log().size(); ++i)
    {
        Json::Value log;
        log["address"] = toJS(receipt->log()[i].address);
        log["topics"] = Json::Value(Json::arrayValue);
        for (unsigned int j = 0; j < receipt->log()[i].topics.size(); ++j)
            log["topics"].append(toJS(receipt->log()[i].topics[j]));
        log["data"] = toJS(receipt->log()[i].data);
        (*response)["logs"].append(log);
    }
    (*response)["logsBloom"] = toJS(receipt->bloom());
    (*response)["status"] = toJS(receipt->status());
    if (g_BCOSConfig.version() > RC3_VERSION)
    {
        (*response)["input"] = toJS(input);
    }
    (*response)["output"] = toJS(receipt->outputBytes());
    return response;
}

std::shared_ptr<Json::Value> Rpc::notifyReceiptWithProof(
    std::weak_ptr<dev::blockchain::BlockChainInterface> _blockChain,
    LocalisedTransactionReceipt::Ptr _receipt, dev::bytesConstRef _input,
    dev::eth::Block::Ptr _blockPtr)
{
    auto response = notifyReceipt(_blockChain, _receipt, _input, _blockPtr);
    // only support merkleProof when supported_version >= v2.2.0
    if (!_blockPtr || g_BCOSConfig.version() < V2_2_0)
    {
        return response;
    }
    // get transaction Proof
    auto index = _receipt->transactionIndex();
    auto blockChain = _blockChain.lock();
    if (!blockChain)
    {
        return response;
    }
    auto txProof = blockChain->getTransactionProof(_blockPtr, index);
    if (!txProof)
    {
        return response;
    }
    addProofToResponse(response, "txProof", txProof);
    // get receipt proof
    auto receiptProof = blockChain->getTransactionReceiptProof(_blockPtr, index);
    if (!receiptProof)
    {
        return response;
    }
    addProofToResponse(response, "receiptProof", receiptProof);
    return response;
}

void Rpc::addProofToResponse(std::shared_ptr<Json::Value> _response, std::string const& _key,
    std::shared_ptr<dev::blockchain::MerkleProofType> _proofList)
{
    uint32_t index = 0;
    for (const auto& merkleItem : *_proofList)
    {
        (*_response)[_key][index]["left"] = Json::arrayValue;
        (*_response)[_key][index]["right"] = Json::arrayValue;
        const auto& left = merkleItem.first;
        for (const auto& item : left)
        {
            (*_response)[_key][index]["left"].append(item);
        }

        const auto& right = merkleItem.second;
        for (const auto& item : right)
        {
            (*_response)[_key][index]["right"].append(item);
        }
        ++index;
    }
}

// send transactions and notify receipts with receipt, transactionProof, receiptProof
std::string Rpc::sendRawTransactionAndGetProof(int _groupID, const std::string& _rlp)
{
    return sendRawTransaction(
        _groupID, _rlp, boost::bind(&Rpc::notifyReceiptWithProof, this, _1, _2, _3, _4));
}

std::string Rpc::sendRawTransaction(int _groupID, const std::string& _rlp)
{
    return sendRawTransaction(
        _groupID, _rlp, boost::bind(&Rpc::notifyReceipt, this, _1, _2, _3, _4));
}


std::string Rpc::sendRawTransaction(int _groupID, const std::string& _rlp,
    std::function<std::shared_ptr<Json::Value>(
        std::weak_ptr<dev::blockchain::BlockChainInterface> _blockChain,
        LocalisedTransactionReceipt::Ptr receipt, dev::bytesConstRef input,
        dev::eth::Block::Ptr _blockPtr)>
        _notifyCallback)
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
        auto blockChain = ledgerManager()->blockChain(_groupID);
        // Transaction tx(jsToBytes(_rlp, OnFailed::Throw), CheckTransaction::Everything);
        Transaction::Ptr tx = std::make_shared<Transaction>(
            jsToBytes(_rlp, OnFailed::Throw), CheckTransaction::Everything);
        // receive transaction from channel or rpc
        tx->setRpcTx(true);
        auto currentTransactionCallback = m_currentTransactionCallback.get();

        uint32_t clientProtocolversion = ProtocolVersion::v1;
        if (currentTransactionCallback)
        {
            auto transactionCallback = *currentTransactionCallback;
            clientProtocolversion = (*m_transactionCallbackVersion)();
            std::weak_ptr<dev::blockchain::BlockChainInterface> weakedBlockChain(blockChain);
            // Note: Since blockChain has a transaction cache, that is,
            //       BlockChain holds transactions, in order to prevent circular references,
            //       the callback of the transaction cannot hold the blockChain of shared_ptr,
            //       must be weak_ptr
            tx->setRpcCallback(
                [weakedBlockChain, _notifyCallback, transactionCallback, clientProtocolversion,
                    _groupID](LocalisedTransactionReceipt::Ptr receipt, dev::bytesConstRef input,
                    dev::eth::Block::Ptr _blockPtr) {
                    std::shared_ptr<Json::Value> response = std::make_shared<Json::Value>();
                    if (clientProtocolversion > 0)
                    {
                        response = _notifyCallback(weakedBlockChain, receipt, input, _blockPtr);
                    }

                    auto receiptContent = response->toStyledString();
                    transactionCallback(receiptContent, _groupID);
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
        RPC_LOG(ERROR) << LOG_DESC("sendRawTransaction exceptioned") << LOG_KV("groupID", _groupID)
                       << LOG_KV("errorMessage", boost::diagnostic_information(e));
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

        auto blockchain = ledgerManager()->blockChain(_groupID);
        checkLedgerStatus(blockchain, "blockChain", "getTransactionByHashWithProof");

        checkRequest(_groupID);
        Json::Value response;
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
        auto blockchain = ledgerManager()->blockChain(_groupID);
        checkLedgerStatus(blockchain, "blockChain", "getTransactionReceiptByHashWithProof");

        checkRequest(_groupID);
        h256 hash = jsToFixed<32>(_transactionHash);
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

Json::Value Rpc::generateGroup(int _groupID, const Json::Value& _params)
{
    RPC_LOG(INFO) << LOG_BADGE("generateGroup") << LOG_DESC("request")
                  << LOG_KV("groupID", _groupID) << LOG_KV("params", _params);

    checkNodeVersionForGroupMgr("generateGroup");

    Json::Value response;
    if (!checkGroupIDForGroupMgr(_groupID, response))
    {
        return response;
    }

    GroupParams groupParams;
    if (!checkParamsForGenerateGroup(_params, groupParams, response))
    {
        return response;
    }

    if (!checkConnection(groupParams.sealers, response))
    {
        return response;
    }

    try
    {
        ledgerManager()->generateGroup(_groupID, groupParams);
        response["code"] = LedgerManagementStatusCode::SUCCESS;
        response["message"] = "Group " + std::to_string(_groupID) + " generated successfully";
    }
#define CATCH_GROUP_ALREADY_EXISTS_EXCEPTION(e)                                        \
    catch (e const&)                                                                   \
    {                                                                                  \
        response["code"] = LedgerManagementStatusCode::GROUP_ALREADY_EXISTS;           \
        response["message"] = "Group " + std::to_string(_groupID) + " already exists"; \
    }
    CATCH_GROUP_ALREADY_EXISTS_EXCEPTION(GroupIsRunning)
    CATCH_GROUP_ALREADY_EXISTS_EXCEPTION(GroupIsStopping)
    CATCH_GROUP_ALREADY_EXISTS_EXCEPTION(GroupAlreadyDeleted)
    CATCH_GROUP_ALREADY_EXISTS_EXCEPTION(GroupAlreadyStopped)
#undef CATCH_GROUP_ALREADY_EXISTS_EXCEPTION
    catch (GenesisConfAlreadyExists const&)
    {
        response["code"] = LedgerManagementStatusCode::GENESIS_CONF_ALREADY_EXISTS;
        response["message"] =
            "Genesis config file for group " + std::to_string(_groupID) + " already exists";
    }
    catch (GroupConfAlreadyExists const&)
    {
        response["code"] = LedgerManagementStatusCode::GROUP_CONF_ALREADY_EXIST;
        response["message"] =
            "Group config file for group " + std::to_string(_groupID) + " already exists";
    }
    catch (std::exception const& _e)
    {
        response["code"] = LedgerManagementStatusCode::INTERNAL_ERROR;
        response["message"] = _e.what();
    }
    return response;
}

Json::Value Rpc::startGroup(int _groupID)
{
    RPC_LOG(INFO) << LOG_BADGE("startGroup") << LOG_DESC("request") << LOG_KV("groupID", _groupID);

    checkNodeVersionForGroupMgr("startGroup");

    Json::Value response;

    if (!checkGroupIDForGroupMgr(_groupID, response))
    {
        return response;
    }

    try
    {
        bool success = m_ledgerInitializer->initLedgerByGroupID(_groupID);
        if (!success)
        {
            throw new dev::Exception("Group" + std::to_string(_groupID) + " initialized failed");
        }

        ledgerManager()->startByGroupID(_groupID);

        response["code"] = LedgerManagementStatusCode::SUCCESS;
        response["message"] = "Group " + std::to_string(_groupID) + " started successfully";
    }
    catch (GenesisConfNotFound const&)
    {
        response["code"] = LedgerManagementStatusCode::GENESIS_CONF_NOT_FOUND;
        response["message"] =
            "Genesis config file for group " + std::to_string(_groupID) + " not found";
    }
    catch (GroupConfNotFound const&)
    {
        response["code"] = LedgerManagementStatusCode::GROUP_CONF_NOT_FOUND;
        response["message"] =
            "Group config file for group " + std::to_string(_groupID) + " not found";
    }
    catch (GroupNotFound const&)
    {
        response["code"] = LedgerManagementStatusCode::GROUP_NOT_FOUND;
        response["message"] = "Group " + std::to_string(_groupID) + " not found";
    }
    catch (GroupIsRunning const&)
    {
        response["code"] = LedgerManagementStatusCode::GROUP_ALREADY_RUNNING;
        response["message"] = "Group " + std::to_string(_groupID) + " is already running";
    }
    catch (GroupIsStopping const&)
    {
        response["code"] = LedgerManagementStatusCode::GROUP_IS_STOPPING;
        response["message"] = "Group " + std::to_string(_groupID) + " is stopping";
    }
    catch (GroupAlreadyDeleted const&)
    {
        response["code"] = LedgerManagementStatusCode::GROUP_ALREADY_DELETED;
        response["message"] = "Group " + std::to_string(_groupID) + " has been deleted";
    }
    catch (std::exception const& _e)
    {
        response["code"] = LedgerManagementStatusCode::INTERNAL_ERROR;
        response["message"] = _e.what();
    }

    return response;
}

Json::Value Rpc::stopGroup(int _groupID)
{
    RPC_LOG(INFO) << LOG_BADGE("stopGroup") << LOG_DESC("request") << LOG_KV("groupID", _groupID);

    checkNodeVersionForGroupMgr("stopGroup");

    Json::Value response;

    if (!checkGroupIDForGroupMgr(_groupID, response))
    {
        return response;
    }

    try
    {
        ledgerManager()->stopByGroupID(_groupID);
        response["code"] = LedgerManagementStatusCode::SUCCESS;
        response["message"] = "Group " + std::to_string(_groupID) + " stopped successfully";
    }
    catch (GroupNotFound const&)
    {
        response["code"] = LedgerManagementStatusCode::GROUP_NOT_FOUND;
        response["message"] = "Group " + std::to_string(_groupID) + " not found";
    }
    catch (GroupIsStopping const&)
    {
        response["code"] = LedgerManagementStatusCode::GROUP_IS_STOPPING;
        response["message"] = "Group " + std::to_string(_groupID) + " is stopping";
    }
    catch (GroupAlreadyStopped const&)
    {
        response["code"] = LedgerManagementStatusCode::GROUP_ALREADY_STOPPED;
        response["message"] = "Group " + std::to_string(_groupID) + " has already been stopped";
    }
    catch (GroupAlreadyDeleted const&)
    {
        response["code"] = LedgerManagementStatusCode::GROUP_ALREADY_DELETED;
        response["message"] = "Group " + std::to_string(_groupID) + " has already been deleted";
    }
    catch (std::exception const& _e)
    {
        response["code"] = LedgerManagementStatusCode::INTERNAL_ERROR;
        response["message"] = _e.what();
    }
    return response;
}

Json::Value Rpc::removeGroup(int _groupID)
{
    RPC_LOG(INFO) << LOG_BADGE("removeGroup") << LOG_DESC("request") << LOG_KV("groupID", _groupID);

    checkNodeVersionForGroupMgr("generateGroup");

    Json::Value response;

    if (!checkGroupIDForGroupMgr(_groupID, response))
    {
        return response;
    }

    try
    {
        ledgerManager()->removeByGroupID(_groupID);
        response["code"] = LedgerManagementStatusCode::SUCCESS;
        response["message"] = "Group " + std::to_string(_groupID) + " deleted successfully";
    }
    catch (GroupNotFound const&)
    {
        response["code"] = LedgerManagementStatusCode::GROUP_NOT_FOUND;
        response["message"] = "Group " + std::to_string(_groupID) + " not found";
    }
    catch (GroupIsRunning const&)
    {
        response["code"] = LedgerManagementStatusCode::GROUP_ALREADY_RUNNING;
        response["message"] = "Group " + std::to_string(_groupID) + " is running";
    }
    catch (GroupIsStopping const&)
    {
        response["code"] = LedgerManagementStatusCode::GROUP_IS_STOPPING;
        response["message"] = "Group " + std::to_string(_groupID) + " is stopping";
    }
    catch (GroupAlreadyDeleted const&)
    {
        response["code"] = LedgerManagementStatusCode::GROUP_ALREADY_DELETED;
        response["message"] = "Group " + std::to_string(_groupID) + " has already been deleted";
    }
    catch (std::exception const& _e)
    {
        response["code"] = LedgerManagementStatusCode::INTERNAL_ERROR;
        response["message"] = _e.what();
    }
    return response;
}

Json::Value Rpc::recoverGroup(int _groupID)
{
    RPC_LOG(INFO) << LOG_BADGE("recoverGroup") << LOG_DESC("request")
                  << LOG_KV("groupID", _groupID);

    checkNodeVersionForGroupMgr("recoverGroup");

    Json::Value response;

    if (!checkGroupIDForGroupMgr(_groupID, response))
    {
        return response;
    }

    try
    {
        ledgerManager()->recoverByGroupID(_groupID);
        response["code"] = LedgerManagementStatusCode::SUCCESS;
        response["message"] = "Group " + std::to_string(_groupID) + " recovered successfully";
    }
    catch (GroupNotFound const&)
    {
        response["code"] = LedgerManagementStatusCode::GROUP_NOT_FOUND;
        response["message"] = "Group " + std::to_string(_groupID) + " not found";
    }
#define CATCH_GROUP_NOT_DELETED_EXCEPTION(e)                                                 \
    catch (e const&)                                                                         \
    {                                                                                        \
        response["code"] = LedgerManagementStatusCode::GROUP_HAS_NOT_DELETED;                \
        response["message"] = "Group " + std::to_string(_groupID) + " has not been deleted"; \
    }
    CATCH_GROUP_NOT_DELETED_EXCEPTION(GroupIsRunning)
    CATCH_GROUP_NOT_DELETED_EXCEPTION(GroupIsStopping)
    CATCH_GROUP_NOT_DELETED_EXCEPTION(GroupAlreadyStopped)
#undef CATCH_GROUP_NOT_DELETED_EXCEPTION
    catch (std::exception const& _e)
    {
        response["code"] = LedgerManagementStatusCode::INTERNAL_ERROR;
        response["message"] = _e.what();
    }
    return response;
}

Json::Value Rpc::queryGroupStatus(int _groupID)
{
    RPC_LOG(INFO) << LOG_BADGE("queryGroupStatus") << LOG_DESC("request")
                  << LOG_KV("groupID", _groupID);

    checkNodeVersionForGroupMgr("queryGroupStatus");

    Json::Value response;
    if (!checkGroupIDForGroupMgr(_groupID, response))
    {
        response["status"] = "";
        return response;
    }

    LedgerStatus status;
    try
    {
        status = ledgerManager()->queryGroupStatus(_groupID);
    }
    catch (UnknownGroupStatus&)
    {
        status = LedgerStatus::UNKNOWN;
    }

    response["code"] = LedgerManagementStatusCode::SUCCESS;
    response["message"] = "";

    switch (status)
    {
    case LedgerStatus::INEXISTENT:
        response["status"] = "INEXISTENT";
        break;
    case LedgerStatus::RUNNING:
        response["status"] = "RUNNING";
        break;
    case LedgerStatus::STOPPING:
        response["status"] = "STOPPING";
        break;
    case LedgerStatus::STOPPED:
        response["status"] = "STOPPED";
        break;
    case LedgerStatus::DELETED:
        response["status"] = "DELETED";
        break;
    case LedgerStatus::UNKNOWN:
        response["status"] = "UNKNOWN";
        response["code"] = LedgerManagementStatusCode::INTERNAL_ERROR;
        response["message"] = "Please check `.group_status` file of the group";
        break;
    }
    return response;
}

void Rpc::checkNodeVersionForGroupMgr(const char* _methodName)
{
    if (g_BCOSConfig.version() < V2_2_0)
    {
        RPC_LOG(ERROR) << _methodName << " only support after by v2.2.0";
        BOOST_THROW_EXCEPTION(JsonRpcException(
            RPCExceptionType::InvalidRequest, "method stopGroup not support this version"));
    }
}

bool Rpc::checkConnection(const std::set<std::string>& _sealerList, Json::Value& _response)
{
    bool flag = true;
    std::string errorInfo;

    for (auto& sealer : _sealerList)
    {
        auto nodeID = NodeID(sealer);
        if (nodeID == service()->id())
        {
            continue;
        }

        if (!service()->isConnected(nodeID))
        {
            errorInfo += sealer + ", ";
            flag = false;
        }
    }

    if (!flag)
    {
        _response["code"] = LedgerManagementStatusCode::PEERS_NOT_CONNECTED;
        _response["message"] =
            "Peer(s) not connected: " + errorInfo.substr(0, errorInfo.length() - 2);
    }
    return flag;
}

bool Rpc::checkGroupIDForGroupMgr(int _groupID, Json::Value& _response)
{
    if (_groupID < 1 || _groupID > dev::maxGroupID)
    {
        _response["code"] = LedgerManagementStatusCode::INVALID_PARAMS;
        _response["message"] = "GroupID should be between 1 and " + std::to_string(maxGroupID);
        return false;
    }
    return true;
}

bool Rpc::checkSealerID(const std::string& _sealer)
{
    if (!dev::isHex(_sealer) || _sealer.length() != 128 || _sealer.compare(0, 2, "0x") == 0)
    {
        return false;
    }
    return true;
}

bool Rpc::checkTimestamp(const std::string& _timestamp)
{
    try
    {
        int64_t cmp = boost::lexical_cast<int64_t>(_timestamp);
        if (cmp < 0)
        {
            return false;
        }
        return _timestamp == std::to_string(cmp);
    }
    catch (...)
    {
        return false;
    }
}

bool Rpc::checkParamsForGenerateGroup(
    const Json::Value& _params, GroupParams& _groupParams, Json::Value& _response)
{
    // check timestamp
    if (!_params.isMember("timestamp") || !_params["timestamp"].isString())
    {
        _response["code"] = LedgerManagementStatusCode::INVALID_PARAMS;
        _response["message"] = "invalid `timestamp` field";
        return false;
    }

    _groupParams.timestamp = _params["timestamp"].asString();
    if (!checkTimestamp(_groupParams.timestamp))
    {
        _response["code"] = LedgerManagementStatusCode::INVALID_PARAMS;
        _response["message"] = "invalid timestamp: " + _groupParams.timestamp;
        return false;
    }

    // check sealers
    if (!_params.isMember("sealers") || !_params["sealers"].isArray())
    {
        _response["code"] = LedgerManagementStatusCode::INVALID_PARAMS;
        _response["message"] = "invalid `sealers` field";
        return false;
    }

    int pos = 1;
    for (auto& sealer : _params["sealers"])
    {
        if (!sealer.isString() || !checkSealerID(sealer.asString()))
        {
            _response["code"] = LedgerManagementStatusCode::INVALID_PARAMS;
            _response["message"] = "invalid sealer ID at position " + std::to_string(pos);
            return false;
        }
        _groupParams.sealers.insert(sealer.asString());
        pos++;
    }

    // check enable_free_storage
    if (!_params.isMember("enable_free_storage"))
    {
        _groupParams.enableFreeStorage = false;
    }
    else if (_params["enable_free_storage"].isBool())
    {
        _groupParams.enableFreeStorage = _params["enable_free_storage"].asBool();
    }
    else
    {
        _response["code"] = LedgerManagementStatusCode::INVALID_PARAMS;
        _response["message"] = "invalid `enable_free_storage` field";
        return false;
    }

    return true;
}
