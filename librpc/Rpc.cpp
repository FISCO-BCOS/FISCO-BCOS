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
#include <libblockchain/BlockChainInterface.h>
#include <libblockverifier/BlockVerifierInterface.h>
#include <libconsensus/ConsensusInterface.h>
#include <libdevcore/CommonData.h>
#include <libdevcore/easylog.h>
#include <libethcore/Common.h>
#include <libethcore/CommonJS.h>
#include <libethcore/Transaction.h>
#include <libexecutive/ExecutionResult.h>
#include <libtxpool/TxPoolInterface.h>
#include <boost/algorithm/hex.hpp>
#include <csignal>

using namespace std;
using namespace jsonrpc;
using namespace dev::rpc;


Rpc::Rpc(std::shared_ptr<dev::ledger::LedgerManager> _ledgerManager,
    std::shared_ptr<dev::p2p::Service> _service)
  : m_ledgerManager(_ledgerManager), m_service(_service)
{}

Json::Value Rpc::blockNumber(const Json::Value& requestJson)
{
    Json::Value responseJson;
    try
    {
        LOG(INFO) << "blockNumber # requestJson = " << requestJson.toStyledString();
        std::string id = requestJson["id"].asString();
        std::string version = requestJson["jsonrpc"].asString();
        responseJson["id"] = id;
        responseJson["jsonrpc"] = version;

        int16_t groupId = requestJson["groupId"].asInt();
        std::string method = requestJson["method"].asString();

        auto blockchain = ledgerManager()->blockChain(groupId);

        responseJson["result"] = toJS(blockchain->number());

        return responseJson;
    }
    catch (std::exception& e)
    {
        return rpcErrorResult(responseJson, e);
    }
}


Json::Value Rpc::pbftView(const Json::Value& requestJson)
{
    Json::Value responseJson;
    try
    {
        LOG(INFO) << "pbftView # requestJson = " << requestJson.toStyledString();
        std::string id = requestJson["id"].asString();
        std::string version = requestJson["jsonrpc"].asString();
        responseJson["id"] = id;
        responseJson["jsonrpc"] = version;

        int16_t groupId = requestJson["groupId"].asInt();
        std::string method = requestJson["method"].asString();

        auto consensus = ledgerManager()->consensus(groupId);

        std::string status = consensus->consensusStatus();
        Json::Reader reader;
        Json::Value statusJson;
        u256 view;
        if (reader.parse(status, statusJson))
        {
            view = statusJson[0]["currentView"].asUInt();
            responseJson["result"] = toJS(view);
        }
        return responseJson;
    }
    catch (std::exception& e)
    {
        return rpcErrorResult(responseJson, e);
    }
}


Json::Value Rpc::peers(const Json::Value& requestJson)
{
    Json::Value responseJson;
    try
    {
        LOG(INFO) << "peers # requestJson = " << requestJson.toStyledString();
        std::string id = requestJson["id"].asString();
        std::string version = requestJson["jsonrpc"].asString();
        responseJson["id"] = id;
        responseJson["jsonrpc"] = version;

        int16_t groupId = requestJson["groupId"].asInt();
        std::string method = requestJson["method"].asString();

        SessionInfos peers = service()->sessionInfos();
        for (SessionInfo const& peer : peers)
        {
            responseJson["result"] = Json::Value(Json::arrayValue);
            Json::Value peerJson;
            peerJson["NodeID"] = peer.nodeID.hex();
            peerJson["IP"] = peer.nodeIPEndpoint.address.to_string();
            peerJson["TcpPort"] = peer.nodeIPEndpoint.tcpPort;
            peerJson["UdpPort"] = peer.nodeIPEndpoint.udpPort;
            peerJson["Topic"] = Json::Value(Json::arrayValue);
            for (std::string topic : peer.topics)
                peerJson["Topic"].append(topic);
            responseJson["result"].append(peerJson);
        }
        return responseJson;
    }
    catch (std::exception& e)
    {
        return rpcErrorResult(responseJson, e);
    }
}


Json::Value Rpc::getBlockByHash(const Json::Value& requestJson)
{
    Json::Value responseJson;
    try
    {
        LOG(INFO) << "getBlockByHash # requestJson = " << requestJson.toStyledString();
        std::string id = requestJson["id"].asString();
        std::string version = requestJson["jsonrpc"].asString();
        responseJson["id"] = id;
        responseJson["jsonrpc"] = version;

        int16_t groupId = requestJson["groupId"].asInt();
        std::string method = requestJson["method"].asString();
        std::string blockHash = requestJson["params"][0].asString();
        bool includeTransactions = requestJson["params"][1].asBool();

        auto blockchain = ledgerManager()->blockChain(groupId);

        h256 hash = jsToFixed<32>(blockHash);
        auto block = blockchain->getBlockByHash(hash);
        responseJson["result"]["number"] = toJS(block->header().number());
        responseJson["result"]["hash"] = blockHash;
        responseJson["result"]["parentHash"] = toJS(block->header().parentHash());
        responseJson["result"]["logsBloom"] = toJS(block->header().logBloom());
        responseJson["result"]["transactionsRoot"] = toJS(block->header().transactionsRoot());
        responseJson["result"]["stateRoot"] = toJS(block->header().stateRoot());
        responseJson["result"]["sealer"] = toJS(block->header().sealer());
        responseJson["result"]["extraData"] = Json::Value(Json::arrayValue);
        auto datas = block->header().extraData();
        for (auto data : datas)
            responseJson["result"]["extraData"].append(toJS(data));
        responseJson["result"]["gasLimit"] = toJS(block->header().gasLimit());
        responseJson["result"]["gasUsed"] = toJS(block->header().gasUsed());
        responseJson["result"]["timestamp"] = toJS(block->header().timestamp());
        auto transactions = block->transactions();
        responseJson["result"]["transactions"] = Json::Value(Json::arrayValue);
        for (unsigned i = 0; i < transactions.size(); i++)
        {
            if (includeTransactions)
            {
                responseJson["result"]["transactions"].append(
                    toJson(transactions[i], std::make_pair(hash, i), block->header().number()));
            }
            else
            {
                responseJson["result"]["transactions"].append(toJS(transactions[i].sha3()));
            }
        }
        return responseJson;
    }
    catch (std::exception& e)
    {
        return rpcErrorResult(responseJson, e);
    }
}


Json::Value Rpc::getBlockByNumber(const Json::Value& requestJson)
{
    Json::Value responseJson;
    try
    {
        LOG(INFO) << "getBlockByNumber # requestJson = " << requestJson.toStyledString();
        std::string id = requestJson["id"].asString();
        std::string version = requestJson["jsonrpc"].asString();
        responseJson["id"] = id;
        responseJson["jsonrpc"] = version;

        int16_t groupId = requestJson["groupId"].asInt();
        std::string method = requestJson["method"].asString();
        std::string blockNumber = requestJson["params"][0].asString();
        bool includeTransactions = requestJson["params"][1].asBool();

        auto blockchain = ledgerManager()->blockChain(groupId);

        BlockNumber number = jsToBlockNumber(blockNumber);
        auto block = blockchain->getBlockByNumber(number);
        responseJson["result"]["number"] = toJS(number);
        responseJson["result"]["hash"] = toJS(block->headerHash());
        responseJson["result"]["parentHash"] = toJS(block->header().parentHash());
        responseJson["result"]["logsBloom"] = toJS(block->header().logBloom());
        responseJson["result"]["transactionsRoot"] = toJS(block->header().transactionsRoot());
        responseJson["result"]["stateRoot"] = toJS(block->header().stateRoot());
        responseJson["result"]["sealer"] = toJS(block->header().sealer());
        responseJson["result"]["extraData"] = Json::Value(Json::arrayValue);
        auto datas = block->header().extraData();
        for (auto data : datas)
            responseJson["result"]["extraData"].append(toJS(data));
        responseJson["result"]["gasLimit"] = toJS(block->header().gasLimit());
        responseJson["result"]["gasUsed"] = toJS(block->header().gasUsed());
        responseJson["result"]["timestamp"] = toJS(block->header().timestamp());
        auto transactions = block->transactions();
        responseJson["result"]["transactions"] = Json::Value(Json::arrayValue);
        for (unsigned i = 0; i < transactions.size(); i++)
        {
            if (includeTransactions)
            {
                responseJson["result"]["transactions"].append(toJson(transactions[i],
                    std::make_pair(block->headerHash(), i), block->header().number()));
            }
            else
            {
                responseJson["result"]["transactions"].append(toJS(transactions[i].sha3()));
            }
        }
        return responseJson;
    }
    catch (std::exception& e)
    {
        return rpcErrorResult(responseJson, e);
    }
}


Json::Value Rpc::getTransactionByHash(const Json::Value& requestJson)
{
    Json::Value responseJson;
    try
    {
        LOG(INFO) << "getTransactionByHash # requestJson = " << requestJson.toStyledString();
        std::string id = requestJson["id"].asString();
        std::string version = requestJson["jsonrpc"].asString();
        responseJson["id"] = id;
        responseJson["jsonrpc"] = version;

        int16_t groupId = requestJson["groupId"].asInt();
        std::string method = requestJson["method"].asString();
        std::string txHash = requestJson["params"][0].asString();

        auto blockchain = ledgerManager()->blockChain(groupId);

        h256 hash = jsToFixed<32>(txHash);
        auto tx = blockchain->getLocalisedTxByHash(hash);
        responseJson["result"]["blockHash"] = toJS(tx.blockHash());
        responseJson["result"]["blockNumber"] = toJS(tx.blockNumber());
        responseJson["result"]["from"] = toJS(tx.from());
        responseJson["result"]["gas"] = toJS(tx.gas());
        responseJson["result"]["gasPrice"] = toJS(tx.gasPrice());
        responseJson["result"]["hash"] = toJS(hash);
        responseJson["result"]["input"] = toJS(tx.data());
        responseJson["result"]["nonce"] = toJS(tx.nonce());
        responseJson["result"]["to"] = toJS(tx.to());
        responseJson["result"]["transactionIndex"] = toJS(tx.transactionIndex());
        responseJson["result"]["value"] = toJS(tx.value());

        return responseJson;
    }
    catch (std::exception& e)
    {
        return rpcErrorResult(responseJson, e);
    }
}


Json::Value Rpc::getTransactionByBlockHashAndIndex(const Json::Value& requestJson)
{
    Json::Value responseJson;
    try
    {
        LOG(INFO) << "getTransactionByBlockHashAndIndex # requestJson = "
                  << requestJson.toStyledString();
        std::string id = requestJson["id"].asString();
        std::string version = requestJson["jsonrpc"].asString();
        responseJson["id"] = id;
        responseJson["jsonrpc"] = version;

        int16_t groupId = requestJson["groupId"].asInt();
        std::string method = requestJson["method"].asString();
        std::string blockHash = requestJson["params"][0].asString();
        int index = jsToInt(requestJson["params"][1].asString());

        auto blockchain = ledgerManager()->blockChain(groupId);

        h256 hash = jsToFixed<32>(blockHash);
        auto block = blockchain->getBlockByHash(hash);
        auto transactions = block->transactions();
        if (transactions[index])
        {
            Transaction tx = transactions[index];
            responseJson["result"]["blockHash"] = blockHash;
            responseJson["result"]["blockNumber"] = toJS(block->header().number());
            responseJson["result"]["from"] = toJS(tx.from());
            responseJson["result"]["gas"] = toJS(tx.gas());
            responseJson["result"]["gasPrice"] = toJS(tx.gasPrice());
            responseJson["result"]["hash"] = toJS(tx.sha3());
            responseJson["result"]["input"] = toJS(tx.data());
            responseJson["result"]["nonce"] = toJS(tx.nonce());
            responseJson["result"]["to"] = toJS(tx.to());
            responseJson["result"]["transactionIndex"] = toJS(index);
            responseJson["result"]["value"] = toJS(tx.value());
        }
        return responseJson;
    }
    catch (std::exception& e)
    {
        return rpcErrorResult(responseJson, e);
    }
}


Json::Value Rpc::getTransactionByBlockNumberAndIndex(const Json::Value& requestJson)
{
    Json::Value responseJson;
    try
    {
        LOG(INFO) << "getTransactionByBlockNumberAndIndex # requestJson = "
                  << requestJson.toStyledString();
        std::string id = requestJson["id"].asString();
        std::string version = requestJson["jsonrpc"].asString();
        responseJson["id"] = id;
        responseJson["jsonrpc"] = version;

        std::string method = requestJson["method"].asString();
        int16_t groupId = requestJson["groupId"].asInt();
        std::string blockNumber = requestJson["params"][0].asString();
        int index = jsToInt(requestJson["params"][1].asString());

        auto blockchain = ledgerManager()->blockChain(groupId);

        BlockNumber number = jsToBlockNumber(blockNumber);
        auto block = blockchain->getBlockByNumber(number);
        Transactions transactions = block->transactions();
        if (transactions[index])
        {
            Transaction tx = transactions[index];
            responseJson["result"]["blockHash"] = toJS(block->header().hash());
            responseJson["result"]["blockNumber"] = toJS(block->header().number());
            responseJson["result"]["from"] = toJS(tx.from());
            responseJson["result"]["gas"] = toJS(tx.gas());
            responseJson["result"]["gasPrice"] = toJS(tx.gasPrice());
            responseJson["result"]["hash"] = toJS(tx.sha3());
            responseJson["result"]["input"] = toJS(tx.data());
            responseJson["result"]["nonce"] = toJS(tx.nonce());
            responseJson["result"]["to"] = toJS(tx.to());
            responseJson["result"]["transactionIndex"] = toJS(index);
            responseJson["result"]["value"] = toJS(tx.value());
        }
        return responseJson;
    }
    catch (std::exception& e)
    {
        return rpcErrorResult(responseJson, e);
    }
}


Json::Value Rpc::getTransactionReceipt(const Json::Value& requestJson)
{
    Json::Value responseJson;
    try
    {
        LOG(INFO) << "getTransactionReceipt # requestJson = " << requestJson.toStyledString();
        std::string id = requestJson["id"].asString();
        std::string version = requestJson["jsonrpc"].asString();
        responseJson["id"] = id;
        responseJson["jsonrpc"] = version;

        int16_t groupId = requestJson["groupId"].asInt();
        std::string method = requestJson["method"].asString();
        std::string txHash = requestJson["params"][0].asString();

        auto blockchain = ledgerManager()->blockChain(groupId);

        h256 hash = jsToFixed<32>(txHash);
        auto tx = blockchain->getLocalisedTxByHash(hash);
        auto txReceipt = blockchain->getTransactionReceiptByHash(hash);
        responseJson["result"]["transactionHash"] = txHash;
        responseJson["result"]["transactionIndex"] = toJS(tx.transactionIndex());
        responseJson["result"]["blockNumber"] = toJS(tx.blockNumber());
        responseJson["result"]["blockHash"] = toJS(tx.blockHash());
        responseJson["result"]["from"] = toJS(tx.from());
        responseJson["result"]["to"] = toJS(tx.to());
        responseJson["result"]["gasUsed"] = toJS(txReceipt.gasUsed());
        responseJson["result"]["contractAddress"] = toJS(txReceipt.contractAddress());
        responseJson["result"]["logs"] = Json::Value(Json::arrayValue);
        for (unsigned int i = 0; i < txReceipt.log().size(); ++i)
        {
            Json::Value log;
            log["address"] = toJS(txReceipt.log()[i].address);
            log["topics"] = toJS(txReceipt.log()[i].topics);
            log["data"] = toJS(txReceipt.log()[i].data);
            responseJson["result"]["logs"].append(log);
        }
        responseJson["result"]["logsBloom"] = toJS(txReceipt.bloom());
        responseJson["result"]["status"] = toJS(txReceipt.status());
        return responseJson;
    }
    catch (std::exception& e)
    {
        return rpcErrorResult(responseJson, e);
    }
}


Json::Value Rpc::pendingTransactions(const Json::Value& requestJson)
{
    Json::Value responseJson;
    try
    {
        LOG(INFO) << "pendingTransactions # requestJson = " << requestJson.toStyledString();
        std::string id = requestJson["id"].asString();
        std::string version = requestJson["jsonrpc"].asString();
        responseJson["id"] = id;
        responseJson["jsonrpc"] = version;

        int16_t groupId = requestJson["groupId"].asInt();
        auto txPool = ledgerManager()->txPool(groupId);
        auto blockchain = ledgerManager()->blockChain(groupId);


        responseJson["result"]["pending"] = Json::Value(Json::arrayValue);
        Transactions transactions = txPool->pendingList();
        for (size_t i = 0; i < transactions.size(); ++i)
        {
            auto tx = blockchain->getLocalisedTxByHash(transactions[i].sha3());
            Json::Value txJson;
            txJson["blockHash"] = toJS(tx.blockHash());
            txJson["blockNumber"] = toJS(tx.blockNumber());
            txJson["from"] = toJS(tx.from());
            txJson["gas"] = toJS(tx.gas());
            txJson["gasPrice"] = toJS(tx.gasPrice());
            txJson["hash"] = toJS(tx.sha3());
            txJson["input"] = toJS(tx.data());
            txJson["nonce"] = toJS(tx.nonce());
            txJson["to"] = toJS(tx.to());
            txJson["transactionIndex"] = toJS(tx.transactionIndex());
            txJson["value"] = toJS(tx.value());
            responseJson["result"]["pending"].append(txJson);
        }
        return responseJson;
    }
    catch (std::exception& e)
    {
        return rpcErrorResult(responseJson, e);
    }
}


Json::Value Rpc::call(const Json::Value& requestJson)
{
    Json::Value responseJson;
    try
    {
        LOG(INFO) << "call # requestJson = " << requestJson.toStyledString();
        std::string id = requestJson["id"].asString();
        std::string version = requestJson["jsonrpc"].asString();
        responseJson["id"] = id;
        responseJson["jsonrpc"] = version;

        int16_t groupId = requestJson["groupId"].asInt();
        std::string method = requestJson["method"].asString();
        TransactionSkeleton txSkeleton = toTransactionSkeleton(requestJson);
        Transaction tx(txSkeleton.value, txSkeleton.gasPrice, txSkeleton.gas, txSkeleton.to,
            txSkeleton.data, txSkeleton.nonce);

        auto blockchain = ledgerManager()->blockChain(groupId);
        BlockNumber blockNumber = blockchain->number();
        auto blockHeader = blockchain->getBlockByNumber(blockNumber)->header();

        auto blockVerfier = ledgerManager()->blockVerifier(groupId);
        auto executionResult = blockVerfier->executeTransaction(blockHeader, tx);

        responseJson["result"] = toJS(executionResult.first.output);
        return responseJson;
    }
    catch (std::exception& e)
    {
        return rpcErrorResult(responseJson, e);
    }
}


Json::Value Rpc::sendRawTransaction(const Json::Value& requestJson)
{
    Json::Value responseJson;
    try
    {
        LOG(INFO) << "sendRawTransaction # requestJson = " << requestJson.toStyledString();
        std::string id = requestJson["id"].asString();
        std::string version = requestJson["jsonrpc"].asString();
        responseJson["id"] = id;
        responseJson["jsonrpc"] = version;

        std::string method = requestJson["method"].asString();
        int16_t groupId = requestJson["groupId"].asInt();
        std::string signedData = requestJson["params"][0].asString();
        Transaction tx(jsToBytes(signedData, OnFailed::Throw), CheckTransaction::Everything);

        auto txPool = ledgerManager()->txPool(groupId);
        std::pair<h256, Address> ret = txPool->submit(tx);
        responseJson["result"] = toJS(ret.first);
        return responseJson;
    }
    catch (std::exception& e)
    {
        return rpcErrorResult(responseJson, e);
    }
}

Json::Value Rpc::rpcErrorResult(Json::Value& responseJson, std::exception& e)
{
    responseJson["error"]["code"] = -1;
    responseJson["error"]["message"] = e.what();
    return responseJson;
}
