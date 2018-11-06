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
#include <jsonrpccpp/common/exception.h>
#include <libdevcore/CommonData.h>
#include <libdevcore/easylog.h>
#include <libethcore/Common.h>
#include <libethcore/CommonJS.h>
#include <libethcore/Transaction.h>
#include <libexecutive/ExecutionResult.h>
#include <libsync/SyncStatus.h>
#include <libtxpool/TxPoolInterface.h>
#include <boost/algorithm/hex.hpp>
#include <csignal>

using namespace jsonrpc;
using namespace dev::rpc;
using namespace dev::sync;


Rpc::Rpc(std::shared_ptr<dev::ledger::LedgerManager> _ledgerManager,
    std::shared_ptr<dev::p2p::Service> _service)
  : m_ledgerManager(_ledgerManager), m_service(_service)
{}

std::string Rpc::blockNumber(int _groupID)
{
    try
    {
        LOG(INFO) << "blockNumber # request = " << std::endl
                  << "{ " << std::endl
                  << "\"_groupID\" : " << _groupID << std::endl
                  << "}";

        auto blockchain = ledgerManager()->blockChain(_groupID);
        if (!blockchain)
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::GroupID, RPCMsg[RPCExceptionType::GroupID]));

        return toJS(blockchain->number());
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (...)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR));
    }
}


std::string Rpc::pbftView(int _groupID)
{
    try
    {
        LOG(INFO) << "pbftView # request = " << std::endl
                  << "{ " << std::endl
                  << "\"_groupID\" : " << _groupID << std::endl
                  << "}";

        auto consensus = ledgerManager()->consensus(_groupID);
        if (!consensus)
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::GroupID, RPCMsg[RPCExceptionType::GroupID]));

        std::string status = consensus->consensusStatus();
        Json::Reader reader;
        Json::Value statusJson;
        u256 view;
        if (!reader.parse(status, statusJson))
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::JsonParse, RPCMsg[RPCExceptionType::JsonParse]));

        view = statusJson[0]["currentView"].asUInt();
        return toJS(view);
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (...)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR));
    }
}

Json::Value Rpc::consensusStatus(int _groupID)
{
    try
    {
        LOG(INFO) << "consensusStatus # request = " << std::endl
                  << "{ " << std::endl
                  << "\"_groupID\" : " << _groupID << std::endl
                  << "}";

        auto consensus = ledgerManager()->consensus(_groupID);
        if (!consensus)
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::GroupID, RPCMsg[RPCExceptionType::GroupID]));

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
    catch (...)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR));
    }
}

Json::Value Rpc::syncStatus(int _groupID)
{
    try
    {
        LOG(INFO) << "syncStatus # request = " << std::endl
                  << "{ " << std::endl
                  << "\"_groupID\" : " << _groupID << std::endl
                  << "}";

        Json::Value response;

        auto sync = ledgerManager()->sync(_groupID);
        if (!sync)
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::GroupID, RPCMsg[RPCExceptionType::GroupID]));

        auto status = sync->status();
        if (status.state == SyncState::Idle)
            response["state"] = "Idle";
        else if (status.state == SyncState::Downloading)
            response["state"] = "Downloading";
        else
            response["state"] = "size";

        response["protocolId"] = status.protocolId;
        response["startBlockNumber"] = status.startBlockNumber;
        response["highestBlockNumber"] = status.highestBlockNumber;
        if (status.majorSyncing)
            response["majorSyncing"] = true;
        else
            response["majorSyncing"] = false;

        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (...)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR));
    }
}


std::string Rpc::version()
{
    try
    {
        auto host = service()->getHost();
        if (host)
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::Host, RPCMsg[RPCExceptionType::Host]));

        return host->clientVersion();
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (...)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR));
    }
}

Json::Value Rpc::peers()
{
    try
    {
        Json::Value response = Json::Value(Json::arrayValue);

        auto host = service()->getHost();
        if (!host)
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::Host, RPCMsg[RPCExceptionType::Host]));

        auto sessions = host->sessions();
        for (auto it = sessions.begin(); it != sessions.end(); ++it)
        {
            auto session = it->second;
            Json::Value node;
            if (!session)
                response.append("This is a not session node.");
            node["NodeID"] = session->id().hex();
            node["IP & Port"] = session->peer()->endpoint().name();
            node["Topic"] = Json::Value(Json::arrayValue);
            for (std::string topic : *(session->topics()))
                node["Topic"].append(topic);
            response.append(node);
        }

        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (...)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR));
    }
}

Json::Value Rpc::groupPeers(int _groupID)
{
    try
    {
        LOG(INFO) << "groupPeers # request = " << std::endl
                  << "{ " << std::endl
                  << "\"_groupID\" : " << _groupID << std::endl
                  << "}";

        Json::Value response = Json::Value(Json::arrayValue);

        auto host = service()->getHost();
        if (!host)
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::Host, RPCMsg[RPCExceptionType::Host]));

        h512s _nodeList;
        bool flag = host->getNodeListByGroupID(_groupID, _nodeList);
        if (!flag)
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::GroupID, RPCMsg[RPCExceptionType::GroupID]));

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
    catch (...)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR));
    }
}

Json::Value Rpc::groupList()
{
    try
    {
        Json::Value response = Json::Value(Json::arrayValue);

        auto groupList = ledgerManager()->getGrouplList();
        for (dev::GROUP_ID id : groupList)
            response.append(id);

        return response;
    }
    catch (...)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR));
    }
}


Json::Value Rpc::getBlockByHash(
    int _groupID, const std::string& _blockHash, bool _includeTransactions)
{
    try
    {
        LOG(INFO) << "getBlockByHash # request = " << std::endl
                  << "{ " << std::endl
                  << "\"_groupID\" : " << _groupID << "," << std::endl
                  << "\"_blockHash\" : " << _blockHash << "," << std::endl
                  << "\"_includeTransactions\" : " << _includeTransactions << std::endl
                  << "}";

        Json::Value response;

        auto blockchain = ledgerManager()->blockChain(_groupID);

        if (!blockchain)
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::GroupID, RPCMsg[RPCExceptionType::GroupID]));

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
        response["sealer"] = toJS(block->header().sealer());
        response["extraData"] = Json::Value(Json::arrayValue);
        auto datas = block->header().extraData();
        for (auto data : datas)
            response["extraData"].append(toJS(data));
        response["gasLimit"] = toJS(block->header().gasLimit());
        response["gasUsed"] = toJS(block->header().gasUsed());
        response["timestamp"] = toJS(block->header().timestamp());
        auto transactions = block->transactions();
        response["transactions"] = Json::Value(Json::arrayValue);
        for (unsigned i = 0; i < transactions.size(); i++)
        {
            if (_includeTransactions)
                response["transactions"].append(
                    toJson(transactions[i], std::make_pair(hash, i), block->header().number()));
            else
                response["transactions"].append(toJS(transactions[i].sha3()));
        }

        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (...)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR));
    }
}


Json::Value Rpc::getBlockByNumber(
    int _groupID, const std::string& _blockNumber, bool _includeTransactions)
{
    try
    {
        LOG(INFO) << "getBlockByNumber # request = " << std::endl
                  << "{ " << std::endl
                  << "\"_groupID\" : " << _groupID << "," << std::endl
                  << "\"_blockNumber\" : " << _blockNumber << "," << std::endl
                  << "\"_includeTransactions\" : " << _includeTransactions << std::endl
                  << "}";

        Json::Value response;

        BlockNumber number = jsToBlockNumber(_blockNumber);
        auto blockchain = ledgerManager()->blockChain(_groupID);
        if (!blockchain)
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::GroupID, RPCMsg[RPCExceptionType::GroupID]));

        auto block = blockchain->getBlockByNumber(number);
        if (!block)
            BOOST_THROW_EXCEPTION(JsonRpcException(
                RPCExceptionType::BlockNumberT, RPCMsg[RPCExceptionType::BlockNumberT]));

        response["number"] = toJS(number);
        response["hash"] = toJS(block->headerHash());
        response["parentHash"] = toJS(block->header().parentHash());
        response["logsBloom"] = toJS(block->header().logBloom());
        response["transactionsRoot"] = toJS(block->header().transactionsRoot());
        response["stateRoot"] = toJS(block->header().stateRoot());
        response["sealer"] = toJS(block->header().sealer());
        response["extraData"] = Json::Value(Json::arrayValue);
        auto datas = block->header().extraData();
        for (auto data : datas)
            response["extraData"].append(toJS(data));
        response["gasLimit"] = toJS(block->header().gasLimit());
        response["gasUsed"] = toJS(block->header().gasUsed());
        response["timestamp"] = toJS(block->header().timestamp());
        auto transactions = block->transactions();
        response["transactions"] = Json::Value(Json::arrayValue);
        for (unsigned i = 0; i < transactions.size(); i++)
        {
            if (_includeTransactions)
                response["transactions"].append(toJson(transactions[i],
                    std::make_pair(block->headerHash(), i), block->header().number()));
            else
                response["transactions"].append(toJS(transactions[i].sha3()));
        }

        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (...)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR));
    }
}


Json::Value Rpc::getTransactionByHash(int _groupID, const std::string& _transactionHash)
{
    try
    {
        LOG(INFO) << "getTransactionByHash # request = " << std::endl
                  << "{ " << std::endl
                  << "\"_groupID\" : " << _groupID << "," << std::endl
                  << "\"_transactionHash\" : " << _transactionHash << std::endl
                  << "}";

        Json::Value response;
        auto blockchain = ledgerManager()->blockChain(_groupID);
        if (!blockchain)
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::GroupID, RPCMsg[RPCExceptionType::GroupID]));

        h256 hash = jsToFixed<32>(_transactionHash);
        auto tx = blockchain->getLocalisedTxByHash(hash);
        response["blockHash"] = toJS(tx.blockHash());
        response["blockNumber"] = toJS(tx.blockNumber());
        response["from"] = toJS(tx.from());
        response["gas"] = toJS(tx.gas());
        response["gasPrice"] = toJS(tx.gasPrice());
        response["hash"] = toJS(hash);
        response["input"] = toJS(tx.data());
        response["nonce"] = toJS(tx.nonce());
        response["to"] = toJS(tx.to());
        response["transactionIndex"] = toJS(tx.transactionIndex());
        response["value"] = toJS(tx.value());

        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (...)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR));
    }
}


Json::Value Rpc::getTransactionByBlockHashAndIndex(
    int _groupID, const std::string& _blockHash, const std::string& _transactionIndex)
{
    try
    {
        LOG(INFO) << "getTransactionByBlockHashAndIndex # request = " << std::endl
                  << "{ " << std::endl
                  << "\"_groupID\" : " << _groupID << "," << std::endl
                  << "\"_blockHash\" : " << _blockHash << "," << std::endl
                  << "\"_transactionIndex\" : " << _transactionIndex << std::endl
                  << "}";

        Json::Value response;

        auto blockchain = ledgerManager()->blockChain(_groupID);
        if (!blockchain)
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::GroupID, RPCMsg[RPCExceptionType::GroupID]));

        h256 hash = jsToFixed<32>(_blockHash);
        auto block = blockchain->getBlockByHash(hash);
        if (!block)
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::BlockHash, RPCMsg[RPCExceptionType::BlockHash]));

        auto transactions = block->transactions();
        unsigned int txIndex = jsToInt(_transactionIndex);
        if (txIndex >= transactions.size())
            BOOST_THROW_EXCEPTION(JsonRpcException(
                RPCExceptionType::TransactionIndex, RPCMsg[RPCExceptionType::TransactionIndex]));

        Transaction tx = transactions[txIndex];
        response["blockHash"] = _blockHash;
        response["blockNumber"] = toJS(block->header().number());
        response["from"] = toJS(tx.from());
        response["gas"] = toJS(tx.gas());
        response["gasPrice"] = toJS(tx.gasPrice());
        response["hash"] = toJS(tx.sha3());
        response["input"] = toJS(tx.data());
        response["nonce"] = toJS(tx.nonce());
        response["to"] = toJS(tx.to());
        response["transactionIndex"] = toJS(txIndex);
        response["value"] = toJS(tx.value());

        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (...)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR));
    }
}


Json::Value Rpc::getTransactionByBlockNumberAndIndex(
    int _groupID, const std::string& _blockNumber, const std::string& _transactionIndex)
{
    try
    {
        LOG(INFO) << "getTransactionByBlockNumberAndIndex # request = " << std::endl
                  << "{ " << std::endl
                  << "\"_groupID\" : " << _groupID << "," << std::endl
                  << "\"_blockNumber\" : " << _blockNumber << "," << std::endl
                  << "\"_transactionIndex\" : " << _transactionIndex << std::endl
                  << "}";

        Json::Value response;

        auto blockchain = ledgerManager()->blockChain(_groupID);
        if (!blockchain)
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::GroupID, RPCMsg[RPCExceptionType::GroupID]));

        BlockNumber number = jsToBlockNumber(_blockNumber);
        auto block = blockchain->getBlockByNumber(number);
        if (!block)
            BOOST_THROW_EXCEPTION(JsonRpcException(
                RPCExceptionType::BlockNumberT, RPCMsg[RPCExceptionType::BlockNumberT]));

        Transactions transactions = block->transactions();
        unsigned int txIndex = jsToInt(_transactionIndex);
        if (txIndex >= transactions.size())
            BOOST_THROW_EXCEPTION(JsonRpcException(
                RPCExceptionType::TransactionIndex, RPCMsg[RPCExceptionType::TransactionIndex]));

        Transaction tx = transactions[txIndex];
        response["blockHash"] = toJS(block->header().hash());
        response["blockNumber"] = toJS(block->header().number());
        response["from"] = toJS(tx.from());
        response["gas"] = toJS(tx.gas());
        response["gasPrice"] = toJS(tx.gasPrice());
        response["hash"] = toJS(tx.sha3());
        response["input"] = toJS(tx.data());
        response["nonce"] = toJS(tx.nonce());
        response["to"] = toJS(tx.to());
        response["transactionIndex"] = toJS(txIndex);
        response["value"] = toJS(tx.value());

        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (...)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR));
    }
}


Json::Value Rpc::getTransactionReceipt(int _groupID, const std::string& _transactionHash)
{
    try
    {
        LOG(INFO) << "getTransactionReceipt # request = " << std::endl
                  << "{ " << std::endl
                  << "\"_groupID\" : " << _groupID << "," << std::endl
                  << "\"_transactionHash\" : " << _transactionHash << std::endl
                  << "}";

        Json::Value response;

        auto blockchain = ledgerManager()->blockChain(_groupID);
        if (!blockchain)
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::GroupID, RPCMsg[RPCExceptionType::GroupID]));

        h256 hash = jsToFixed<32>(_transactionHash);
        auto tx = blockchain->getLocalisedTxByHash(hash);
        auto txReceipt = blockchain->getTransactionReceiptByHash(hash);
        response["transactionHash"] = _transactionHash;
        response["transactionIndex"] = toJS(tx.transactionIndex());
        response["blockNumber"] = toJS(tx.blockNumber());
        response["blockHash"] = toJS(tx.blockHash());
        response["from"] = toJS(tx.from());
        response["to"] = toJS(tx.to());
        response["gasUsed"] = toJS(txReceipt.gasUsed());
        response["contractAddress"] = toJS(txReceipt.contractAddress());
        response["logs"] = Json::Value(Json::arrayValue);
        for (unsigned int i = 0; i < txReceipt.log().size(); ++i)
        {
            Json::Value log;
            log["address"] = toJS(txReceipt.log()[i].address);
            log["topics"] = toJS(txReceipt.log()[i].topics);
            log["data"] = toJS(txReceipt.log()[i].data);
            response["logs"].append(log);
        }
        response["logsBloom"] = toJS(txReceipt.bloom());
        response["status"] = toJS(txReceipt.status());

        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (...)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR));
    }
}


Json::Value Rpc::pendingTransactions(int _groupID)
{
    try
    {
        LOG(INFO) << "pendingTransactions # request = " << std::endl
                  << "{ " << std::endl
                  << "\"_groupID\" : " << _groupID << std::endl
                  << "}";

        Json::Value response;

        auto txPool = ledgerManager()->txPool(_groupID);
        if (!txPool)
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::GroupID, RPCMsg[RPCExceptionType::GroupID]));

        response["pending"] = Json::Value(Json::arrayValue);
        Transactions transactions = txPool->pendingList();
        for (size_t i = 0; i < transactions.size(); ++i)
        {
            auto tx = transactions[i];
            Json::Value txJson;
            txJson["from"] = toJS(tx.from());
            txJson["gas"] = toJS(tx.gas());
            txJson["gasPrice"] = toJS(tx.gasPrice());
            txJson["hash"] = toJS(tx.sha3());
            txJson["input"] = toJS(tx.data());
            txJson["nonce"] = toJS(tx.nonce());
            txJson["to"] = toJS(tx.to());
            txJson["value"] = toJS(tx.value());
            response["pending"].append(txJson);
        }

        return response;
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (...)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR));
    }
}


std::string Rpc::call(int _groupID, const Json::Value& request)
{
    try
    {
        LOG(INFO) << "call # request = " << std::endl
                  << "{ " << std::endl
                  << "\"_groupID\" : " << _groupID << "," << std::endl
                  << request.toStyledString() << "}";

        auto blockchain = ledgerManager()->blockChain(_groupID);
        auto blockverfier = ledgerManager()->blockVerifier(_groupID);
        if (!blockchain || !blockverfier)
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::GroupID, RPCMsg[RPCExceptionType::GroupID]));

        BlockNumber blockNumber = blockchain->number();
        auto block = blockchain->getBlockByNumber(blockNumber);
        if (!block)
            BOOST_THROW_EXCEPTION(JsonRpcException(
                RPCExceptionType::BlockNumberT, RPCMsg[RPCExceptionType::BlockNumberT]));

        TransactionSkeleton txSkeleton = toTransactionSkeleton(request);
        Transaction tx(txSkeleton.value, txSkeleton.gasPrice, txSkeleton.gas, txSkeleton.to,
            txSkeleton.data, txSkeleton.nonce);
        auto blockHeader = block->header();
        auto executionResult = blockverfier->executeTransaction(blockHeader, tx);

        return toJS(executionResult.first.output);
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (...)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR));
    }
}

std::string Rpc::sendRawTransaction(int _groupID, const std::string& _rlp)
{
    try
    {
        LOG(INFO) << "sendRawTransaction # request = " << std::endl
                  << "{ " << std::endl
                  << "\"_groupID\" : " << _groupID << "," << std::endl
                  << "\"_rlp\" : " << _rlp << std::endl
                  << "}";

        auto txPool = ledgerManager()->txPool(_groupID);
        if (!txPool)
            BOOST_THROW_EXCEPTION(
                JsonRpcException(RPCExceptionType::GroupID, RPCMsg[RPCExceptionType::GroupID]));

        Transaction tx(jsToBytes(_rlp, OnFailed::Throw), CheckTransaction::Everything);
        std::pair<h256, Address> ret = txPool->submit(tx);

        return toJS(ret.first);
    }
    catch (JsonRpcException& e)
    {
        throw e;
    }
    catch (...)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INTERNAL_ERROR));
    }
}
