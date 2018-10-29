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


Rpc::Rpc(std::shared_ptr<dev::ledger::LedgerManager> _ledgerManager, std::shared_ptr<dev::p2p::Service> _service)
  : m_ledgerManager(_ledgerManager), m_service(_service)
{}

Json::Value Rpc::blockNumber(const Json::Value& requestJson)
{
    try
    {
        int16_t groudId = requestJson["groudId"].asInt();
        std::string version = requestJson["jsonrpc"].asString();
        std::string method = requestJson["method"].asString();
        std::string id = requestJson["id"].asString();

        auto blockchain = ledgerManager()->blockChain(groudId);

        Json::Value responseJson;
        responseJson["id"] = id;
        responseJson["jsonrpc"] = version;
        responseJson["result"] = toJS(blockchain->number());

        return responseJson;
    }
    catch (...)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INVALID_PARAMS));
    }
    return Json::Value(Json::nullValue);
}


Json::Value Rpc::pbftView(const Json::Value& requestJson)
{
    try
    {
        int16_t groudId = requestJson["groudId"].asInt();
        std::string version = requestJson["jsonrpc"].asString();
        std::string method = requestJson["method"].asString();
        std::string id = requestJson["id"].asString();

        auto consensus = ledgerManager()->consensus(groudId);

        Json::Value responseJson;
        responseJson["id"] = id;
        responseJson["jsonrpc"] = version;
        std::string status = consensus->consensusStatus();
        Json::Reader reader;
        Json::Value statusJson;
        u256 view;
        if (reader.parse(status, statusJson))
        {
            view = statusJson["currentView"].asUInt();
            responseJson["result"] = toJS(view);
        }
        return responseJson;
    }
    catch (...)
    {
        //BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INVALID_PARAMS));
    }
    return Json::Value(Json::nullValue);
}


Json::Value Rpc::peers(const Json::Value& requestJson)
{
    try
    {
        int16_t groudId = requestJson["groudId"].asInt();
        std::string version = requestJson["jsonrpc"].asString();
        std::string method = requestJson["method"].asString();
        std::string id = requestJson["id"].asString();

        Json::Value responseJson;

        responseJson["id"] = id;
        responseJson["jsonrpc"] = version;
        SessionInfos peers = service()->sessionInfos();
        for (SessionInfo const& peer : peers)
        {
            responseJson["result"] = Json::Value(Json::arrayValue);
            Json::Value peerJson;
            peerJson["NodeID"] = peer.nodeID.hex();
            peerJson["IP"] = peer.nodeIPEndpoint.host;
            peerJson["TcpPort"] = toString(peer.nodeIPEndpoint.tcpPort);
            peerJson["UdpPort"] = toString(peer.nodeIPEndpoint.udpPort);
            responseJson["result"].append(peerJson);
        }
        return responseJson;
    }
    catch (...)
    {
        //BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INVALID_PARAMS));
    }
    return Json::Value(Json::nullValue);
}


Json::Value Rpc::getBlockByHash(const Json::Value& requestJson)
{
    try
    {
        int16_t groudId = requestJson["groudId"].asInt();
        std::string version = requestJson["jsonrpc"].asString();
        std::string method = requestJson["method"].asString();
        std::string blockHash = requestJson["params"][0].asString();
        bool includeTransactions = requestJson["params"][1].asBool();
        std::string id = requestJson["id"].asString();

        auto blockchain = ledgerManager()->blockChain(groudId);

        Json::Value responseJson;
        responseJson["id"] = id;
        responseJson["jsonrpc"] = version;
        h256 hash = jsToFixed<32>(blockHash);
        auto block = blockchain->getBlockByHash(hash);
        responseJson["result"]["number"] = toJS(block->header().number());
        responseJson["result"]["hash"] = blockHash;
        responseJson["result"]["parentHash"] = toJS(block->header().parentHash());
        responseJson["result"]["logsBloom"] = toJS(block->header().logBloom());
        responseJson["result"]["transactionsRoot"] = toJS(block->header().transactionsRoot());
        responseJson["result"]["stateRoot"] = toJS(block->header().stateRoot());
        responseJson["result"]["sealer"] = toJS(block->header().sealer());
        responseJson["result"]["extraData"] = toJS(block->header().extraData());
        responseJson["result"]["gasLimit"] = toJS(block->header().gasLimit());
        responseJson["result"]["gasUsed"] = toJS(block->header().gasUsed());
        responseJson["result"]["timestamp"] = toJS(block->header().timestamp());
        auto transactions = block->transactions();
        responseJson["transactions"] = Json::Value(Json::arrayValue);
        for (unsigned i = 0; i < transactions.size(); i++)
        {
            if (includeTransactions)
            {
                responseJson["transactions"].append(
                    toJson(transactions[i], std::make_pair(hash, i), block->header().number()));
            }
            else
            {
                responseJson["transactions"].append(toJS(transactions[i].sha3()));
            }
        }
        return responseJson;
    }
    catch (...)
    {
        //BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INVALID_PARAMS));
    }
    return Json::Value(Json::nullValue);
}


Json::Value Rpc::getBlockByNumber(const Json::Value& requestJson)
{
    try
    {
        int16_t groudId = requestJson["groudId"].asInt();
        std::string version = requestJson["jsonrpc"].asString();
        std::string method = requestJson["method"].asString();
        std::string blockNumber = requestJson["params"][0].asString();
        bool includeTransactions = requestJson["params"][1].asBool();
        std::string id = requestJson["id"].asString();

        auto blockchain = ledgerManager()->blockChain(groudId);

        Json::Value responseJson;
        responseJson["id"] = id;
        responseJson["jsonrpc"] = version;
        BlockNumber number = jsToBlockNumber(blockNumber);
        auto block = blockchain->getBlockByNumber(number);
        responseJson["result"]["number"] = toJS(number);
        responseJson["result"]["hash"] = toJS(block->headerHash());
        responseJson["result"]["parentHash"] = toJS(block->header().parentHash());
        responseJson["result"]["logsBloom"] = toJS(block->header().logBloom());
        responseJson["result"]["transactionsRoot"] = toJS(block->header().transactionsRoot());
        responseJson["result"]["stateRoot"] = toJS(block->header().stateRoot());
        responseJson["result"]["sealer"] = toJS(block->header().sealer());
        responseJson["result"]["extraData"] = toJS(block->header().extraData());
        responseJson["result"]["gasLimit"] = toJS(block->header().gasLimit());
        responseJson["result"]["gasUsed"] = toJS(block->header().gasUsed());
        responseJson["result"]["timestamp"] = toJS(block->header().timestamp());
        auto transactions = block->transactions();
        responseJson["transactions"] = Json::Value(Json::arrayValue);
        for (unsigned i = 0; i < transactions.size(); i++)
        {
            if (includeTransactions)
            {
                responseJson["transactions"].append(
                    toJson(transactions[i], std::make_pair(block->headerHash(), i), number));
            }
            else
            {
                responseJson["transactions"].append(toJS(transactions[i].sha3()));
            }
        }
        return responseJson;
    }
    catch (...)
    {
        //BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INVALID_PARAMS));
    }
    return Json::Value(Json::nullValue);
}


Json::Value Rpc::getTransactionByHash(const Json::Value& requestJson)
{
    try
    {
        int16_t groudId = requestJson["groudId"].asInt();
        std::string version = requestJson["jsonrpc"].asString();
        std::string method = requestJson["method"].asString();
        std::string txHash = requestJson["params"][0].asString();
        std::string id = requestJson["id"].asString();

        auto blockchain = ledgerManager()->blockChain(groudId);

        Json::Value responseJson;
        responseJson["id"] = id;
        responseJson["jsonrpc"] = version;
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
    catch (...)
    {
        //BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INVALID_PARAMS));
    }
    return Json::Value(Json::nullValue);
}


Json::Value Rpc::getTransactionByBlockHashAndIndex(const Json::Value& requestJson)
{
    try
    {
        int16_t groudId = requestJson["groudId"].asInt();
        std::string version = requestJson["jsonrpc"].asString();
        std::string method = requestJson["method"].asString();
        std::string blockHash = requestJson["params"][0].asString();
        int index = jsToInt(requestJson["params"][1].asString());
        std::string id = requestJson["id"].asString();

        auto blockchain = ledgerManager()->blockChain(groudId);

        Json::Value responseJson;
        responseJson["id"] = id;
        responseJson["jsonrpc"] = version;
        h256 hash = jsToFixed<32>(blockHash);
        auto block = blockchain->getBlockByHash(hash);
        auto transactions = block->transactions();
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

            return responseJson;
        }
    }
    catch (...)
    {
        //BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INVALID_PARAMS));
    }
    return Json::Value(Json::nullValue);
}


Json::Value Rpc::getTransactionByBlockNumberAndIndex(const Json::Value& requestJson)
{
    try
    {
        int16_t groudId = requestJson["groudId"].asInt();
        std::string version = requestJson["jsonrpc"].asString();
        std::string method = requestJson["method"].asString();
        std::string blockNumber = requestJson["params"][0].asString();
        int index = jsToInt(requestJson["params"][1].asString());
        std::string id = requestJson["id"].asString();

        auto blockchain = ledgerManager()->blockChain(groudId);

        Json::Value responseJson;
        responseJson["id"] = id;
        responseJson["jsonrpc"] = version;
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
    catch (...)
    {
        //BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INVALID_PARAMS));
    }
    return Json::Value(Json::nullValue);
}


Json::Value Rpc::getTransactionReceipt(const Json::Value& requestJson)
{
    try
    {
        int16_t groudId = requestJson["groudId"].asInt();
        std::string version = requestJson["jsonrpc"].asString();
        std::string method = requestJson["method"].asString();
        std::string txHash = requestJson["params"][0].asString();
        std::string id = requestJson["id"].asString();

        auto blockchain = ledgerManager()->blockChain(groudId);

        Json::Value responseJson;
        responseJson["id"] = id;
        responseJson["jsonrpc"] = version;
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
    catch (...)
    {
        //BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INVALID_PARAMS));
    }
    return Json::Value(Json::nullValue);
}


Json::Value Rpc::pendingTransactions(const Json::Value& requestJson)
{
    try
    {
        int16_t groudId = requestJson["groudId"].asInt();
        std::string version = requestJson["jsonrpc"].asString();
        std::string id = requestJson["id"].asString();

        auto txPool = ledgerManager()->txPool(groudId);
        auto blockchain = ledgerManager()->blockChain(groudId);

        Json::Value responseJson;
        responseJson["id"] = id;
        responseJson["jsonrpc"] = version;

        responseJson["result"]["pending"] = Json::Value(Json::arrayValue);
        size_t size = txPool->pendingSize();
        Transactions transactions = txPool->pendingList();
        for (size_t i = 0; i < size; ++i)
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
    catch (...)
    {
        //BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INVALID_PARAMS));
    }
    return Json::Value(Json::nullValue);
}


Json::Value Rpc::call(const Json::Value& requestJson)
{
    try
    {
        int16_t groudId = requestJson["groudId"].asInt();
        std::string version = requestJson["jsonrpc"].asString();
        std::string method = requestJson["method"].asString();
        std::string id = requestJson["id"].asString();
        // construct transaction by request parameters
        TransactionSkeleton txSkeleton = toTransactionSkeleton(requestJson);
        Transaction tx(txSkeleton.value, txSkeleton.gasPrice, txSkeleton.gas, txSkeleton.to,
            txSkeleton.data, txSkeleton.nonce);

        auto blockchain = ledgerManager()->blockChain(groudId);
        BlockNumber blockNumber = blockchain->number();
        h256 blockHash = blockchain->numberHash(blockNumber);
        auto blockHeader = blockchain->getBlockByHash(blockHash)->header();

        auto blockVerfier = ledgerManager()->blockVerifier(groudId);
        auto executionResult = blockVerfier->executeTransaction(blockHeader, tx);

        LOG(DEBUG) << "eth_call # _json = " << requestJson.toStyledString();

        Json::Value responseJson;
        responseJson["id"] = id;
        responseJson["jsonrpc"] = version;
        responseJson["result"] = toJS(executionResult.first.output);
        return responseJson;
    }
    catch (...)
    {
        //BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INVALID_PARAMS));
    }
    return Json::Value(Json::nullValue);
}


Json::Value Rpc::sendRawTransaction(const Json::Value& requestJson)
{
    try
    {
        int16_t groudId = requestJson["groudId"].asInt();
        std::string version = requestJson["jsonrpc"].asString();
        std::string method = requestJson["method"].asString();
        std::string id = requestJson["id"].asString();
        std::string signedData = requestJson["params"][0].asString();
        // construct transaction by request signedData rlp
        Transaction tx(jsToBytes(signedData, OnFailed::Throw), CheckTransaction::Everything);

        auto txPool = ledgerManager()->txPool(groudId);
        std::pair<h256, Address> ret = txPool->submit(tx);
        Json::Value responseJson;
        responseJson["id"] = id;
        responseJson["jsonrpc"] = version;
        if (ret.first)
        {
            h256 txHash = ret.first;
            responseJson["result"] = toJS(txHash);
        }
        else
        {
            responseJson["result"] = toJS(h256(0));
        }
        return responseJson;
    }
    catch (...)
    {
        //BOOST_THROW_EXCEPTION(JsonRpcException(Errors::ERROR_RPC_INVALID_PARAMS));
    }
    return Json::Value(Json::nullValue);
}
