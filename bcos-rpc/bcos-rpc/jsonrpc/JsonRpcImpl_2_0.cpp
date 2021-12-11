/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief: implement for the RPC
 * @file: JsonRpcImpl_2_0.h
 * @author: octopus
 * @date: 2021-07-09
 */
#include "libutilities/DataConvertUtility.h"
#include <bcos-framework/interfaces/protocol/Transaction.h>
#include <bcos-framework/interfaces/protocol/TransactionReceipt.h>
#include <bcos-framework/libprotocol/LogEntry.h>
#include <bcos-framework/libprotocol/TransactionStatus.h>
#include <bcos-framework/libutilities/Base64.h>
#include <bcos-framework/libutilities/Log.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/jsonrpc/JsonRpcImpl_2_0.h>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <string>

using namespace std;
using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::group;
using namespace boost::iterators;
using namespace boost::archive::iterators;

void JsonRpcImpl_2_0::initMethod()
{
    m_methodToFunc["call"] =
        std::bind(&JsonRpcImpl_2_0::callI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["sendTransaction"] = std::bind(
        &JsonRpcImpl_2_0::sendTransactionI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getTransaction"] = std::bind(
        &JsonRpcImpl_2_0::getTransactionI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getTransactionReceipt"] = std::bind(&JsonRpcImpl_2_0::getTransactionReceiptI,
        this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getBlockByHash"] = std::bind(
        &JsonRpcImpl_2_0::getBlockByHashI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getBlockByNumber"] = std::bind(
        &JsonRpcImpl_2_0::getBlockByNumberI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getBlockHashByNumber"] = std::bind(&JsonRpcImpl_2_0::getBlockHashByNumberI,
        this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getBlockNumber"] = std::bind(
        &JsonRpcImpl_2_0::getBlockNumberI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getCode"] =
        std::bind(&JsonRpcImpl_2_0::getCodeI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getSealerList"] = std::bind(
        &JsonRpcImpl_2_0::getSealerListI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getObserverList"] = std::bind(
        &JsonRpcImpl_2_0::getObserverListI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getPbftView"] = std::bind(
        &JsonRpcImpl_2_0::getPbftViewI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getPendingTxSize"] = std::bind(
        &JsonRpcImpl_2_0::getPendingTxSizeI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getSyncStatus"] = std::bind(
        &JsonRpcImpl_2_0::getSyncStatusI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getConsensusStatus"] = std::bind(
        &JsonRpcImpl_2_0::getConsensusStatusI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getSystemConfigByKey"] = std::bind(&JsonRpcImpl_2_0::getSystemConfigByKeyI,
        this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getTotalTransactionCount"] =
        std::bind(&JsonRpcImpl_2_0::getTotalTransactionCountI, this, std::placeholders::_1,
            std::placeholders::_2);
    m_methodToFunc["getPeers"] =
        std::bind(&JsonRpcImpl_2_0::getPeersI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getGroupPeers"] = std::bind(
        &JsonRpcImpl_2_0::getGroupPeersI, this, std::placeholders::_1, std::placeholders::_2);

    m_methodToFunc["getGroupList"] = std::bind(
        &JsonRpcImpl_2_0::getGroupListI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getGroupInfo"] = std::bind(
        &JsonRpcImpl_2_0::getGroupInfoI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getGroupInfoList"] = std::bind(
        &JsonRpcImpl_2_0::getGroupInfoListI, this, std::placeholders::_1, std::placeholders::_2);
    m_methodToFunc["getGroupNodeInfo"] = std::bind(
        &JsonRpcImpl_2_0::getGroupNodeInfoI, this, std::placeholders::_1, std::placeholders::_2);

    for (const auto& method : m_methodToFunc)
    {
        RPC_IMPL_LOG(INFO) << LOG_BADGE("initMethod") << LOG_KV("method", method.first);
    }
    RPC_IMPL_LOG(INFO) << LOG_BADGE("initMethod") << LOG_KV("size", m_methodToFunc.size());
}

std::string JsonRpcImpl_2_0::encodeData(bcos::bytesConstRef _data)
{
    return base64Encode(_data);
}

std::shared_ptr<bcos::bytes> JsonRpcImpl_2_0::decodeData(const std::string& _data)
{
    return base64DecodeBytes(_data);
}

void JsonRpcImpl_2_0::parseRpcRequestJson(
    const std::string& _requestBody, JsonRequest& _jsonRequest)
{
    Json::Value root;
    Json::Reader jsonReader;
    std::string errorMessage;

    try
    {
        std::string jsonrpc = "";
        std::string method = "";
        int64_t id = 0;
        do
        {
            if (!jsonReader.parse(_requestBody, root))
            {
                errorMessage = "invalid request json object";
                break;
            }

            if (!root.isMember("jsonrpc"))
            {
                errorMessage = "request has no jsonrpc field";
                break;
            }
            jsonrpc = root["jsonrpc"].asString();

            if (!root.isMember("method"))
            {
                errorMessage = "request has no method field";
                break;
            }
            method = root["method"].asString();

            if (root.isMember("id"))
            {
                id = root["id"].asInt64();
            }

            if (!root.isMember("params"))
            {
                errorMessage = "request has no params field";
                break;
            }

            if (!root["params"].isArray())
            {
                errorMessage = "request params is not array object";
                break;
            }

            auto jParams = root["params"];

            _jsonRequest.jsonrpc = jsonrpc;
            _jsonRequest.method = method;
            _jsonRequest.id = id;
            _jsonRequest.params = jParams;

            // RPC_IMPL_LOG(DEBUG) << LOG_BADGE("parseRpcRequestJson") << LOG_KV("method", method)
            //                     << LOG_KV("requestMessage", _requestBody);

            // success return
            return;

        } while (0);
    }
    catch (const std::exception& e)
    {
        RPC_IMPL_LOG(ERROR) << LOG_BADGE("parseRpcRequestJson") << LOG_KV("request", _requestBody)
                            << LOG_KV("error", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(
            JsonRpcException(JsonRpcError::ParseError, "Invalid JSON was received by the server."));
    }

    RPC_IMPL_LOG(ERROR) << LOG_BADGE("parseRpcRequestJson") << LOG_KV("request", _requestBody)
                        << LOG_KV("errorMessage", errorMessage);

    BOOST_THROW_EXCEPTION(JsonRpcException(
        JsonRpcError::InvalidRequest, "The JSON sent is not a valid Request object."));
}

void JsonRpcImpl_2_0::parseRpcResponseJson(
    const std::string& _responseBody, JsonResponse& _jsonResponse)
{
    Json::Value root;
    Json::Reader jsonReader;
    std::string errorMessage;

    try
    {
        do
        {
            if (!jsonReader.parse(_responseBody, root))
            {
                errorMessage = "invalid response json object";
                break;
            }

            if (!root.isMember("jsonrpc"))
            {
                errorMessage = "response has no jsonrpc field";
                break;
            }
            _jsonResponse.jsonrpc = root["jsonrpc"].asString();

            if (!root.isMember("id"))
            {
                errorMessage = "response has no id field";
                break;
            }
            _jsonResponse.id = root["id"].asInt64();

            if (root.isMember("error"))
            {
                auto jError = root["error"];
                _jsonResponse.error.code = jError["code"].asInt64();
                _jsonResponse.error.message = jError["message"].asString();
            }

            if (root.isMember("result"))
            {
                _jsonResponse.result = root["result"];
            }

            RPC_IMPL_LOG(TRACE) << LOG_BADGE("parseRpcResponseJson")
                                << LOG_KV("jsonrpc", _jsonResponse.jsonrpc)
                                << LOG_KV("id", _jsonResponse.id)
                                << LOG_KV("error", _jsonResponse.error.toString())
                                << LOG_KV("responseBody", _responseBody);

            return;
        } while (0);
    }
    catch (std::exception& e)
    {
        RPC_IMPL_LOG(ERROR) << LOG_BADGE("parseRpcResponseJson")
                            << LOG_KV("response", _responseBody)
                            << LOG_KV("error", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(
            JsonRpcException(JsonRpcError::ParseError, "Invalid JSON was received by the server."));
    }

    RPC_IMPL_LOG(ERROR) << LOG_BADGE("parseRpcResponseJson") << LOG_KV("response", _responseBody)
                        << LOG_KV("errorMessage", errorMessage);

    BOOST_THROW_EXCEPTION(JsonRpcException(
        JsonRpcError::InvalidRequest, "The JSON sent is not a valid Response object."));
}

std::string JsonRpcImpl_2_0::toStringResponse(const JsonResponse& _jsonResponse)
{
    auto jResp = toJsonResponse(_jsonResponse);
    Json::FastWriter writer;
    std::string resp = writer.write(jResp);
    return resp;
}

Json::Value JsonRpcImpl_2_0::toJsonResponse(const JsonResponse& _jsonResponse)
{
    Json::Value jResp;
    jResp["jsonrpc"] = _jsonResponse.jsonrpc;
    jResp["id"] = _jsonResponse.id;

    if (_jsonResponse.error.code == 0)
    {  // success
        jResp["result"] = _jsonResponse.result;
    }
    else
    {  // error
        Json::Value jError;
        jError["code"] = _jsonResponse.error.code;
        jError["message"] = _jsonResponse.error.message;
        jResp["error"] = jError;
    }

    return jResp;
}

void JsonRpcImpl_2_0::onRPCRequest(const std::string& _requestBody, Sender _sender)
{
    JsonRequest request;
    JsonResponse response;
    try
    {
        parseRpcRequestJson(_requestBody, request);

        response.jsonrpc = request.jsonrpc;
        response.id = request.id;

        const auto& method = request.method;
        auto it = m_methodToFunc.find(method);
        if (it == m_methodToFunc.end())
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(
                JsonRpcError::MethodNotFound, "The method does not exist/is not available."));
        }

        it->second(request.params,
            [_requestBody, response, _sender](Error::Ptr _error, Json::Value& _result) mutable {
                if (_error && (_error->errorCode() != bcos::protocol::CommonError::SUCCESS))
                {
                    // error
                    response.error.code = _error->errorCode();
                    response.error.message = _error->errorMessage();
                }
                else
                {
                    response.result.swap(_result);
                }
                auto strResp = toStringResponse(response);
                _sender(strResp);
                RPC_IMPL_LOG(TRACE) << LOG_BADGE("onRPCRequest") << LOG_KV("request", _requestBody)
                                    << LOG_KV("response", strResp);
            });

        // success response
        return;
    }
    catch (const JsonRpcException& e)
    {
        response.error.code = e.code();
        response.error.message = std::string(e.what());
    }
    catch (const std::exception& e)
    {
        // server internal error or unexpected error
        response.error.code = JsonRpcError::InvalidRequest;
        response.error.message = std::string(e.what());
    }

    auto strResp = toStringResponse(response);
    // error response
    _sender(strResp);

    // RPC_IMPL_LOG(DEBUG) << LOG_BADGE("onRPCRequest") << LOG_KV("request", _requestBody)
    //                     << LOG_KV("response", strResp);
}

void JsonRpcImpl_2_0::toJsonResp(
    Json::Value& jResp, bcos::protocol::Transaction::ConstPtr _transactionPtr)
{
    // transaction version
    jResp["version"] = _transactionPtr->version();
    // transaction hash
    jResp["hash"] = toHexStringWithPrefix(_transactionPtr->hash());
    // transaction nonce
    jResp["nonce"] = _transactionPtr->nonce().str(16);
    // blockLimit
    jResp["blockLimit"] = _transactionPtr->blockLimit();
    // the receiver address
    jResp["to"] = string(_transactionPtr->to());
    // the sender address
    jResp["from"] = toHexStringWithPrefix(_transactionPtr->sender());
    // the input data
    jResp["input"] = toHexStringWithPrefix(_transactionPtr->input());
    // importTime
    jResp["importTime"] = _transactionPtr->importTime();
    // the chainID
    jResp["chainID"] = std::string(_transactionPtr->chainId());
    // the groupID
    jResp["groupID"] = std::string(_transactionPtr->groupId());
    // the signature
    jResp["signature"] = toHexStringWithPrefix(_transactionPtr->signatureData());
}

void JsonRpcImpl_2_0::toJsonResp(Json::Value& jResp, const std::string& _txHash,
    bcos::protocol::TransactionReceipt::ConstPtr _transactionReceiptPtr)
{
    jResp["version"] = _transactionReceiptPtr->version();
    jResp["contractAddress"] = string(_transactionReceiptPtr->contractAddress());
    jResp["gasUsed"] = _transactionReceiptPtr->gasUsed().str(16);
    jResp["status"] = _transactionReceiptPtr->status();
    jResp["blockNumber"] = _transactionReceiptPtr->blockNumber();
    jResp["output"] = toHexStringWithPrefix(_transactionReceiptPtr->output());
    jResp["transactionHash"] = _txHash;
    jResp["hash"] = _transactionReceiptPtr->hash().hexPrefixed();
    jResp["logEntries"] = Json::Value(Json::arrayValue);
    for (const auto& logEntry : _transactionReceiptPtr->logEntries())
    {
        Json::Value jLog;
        jLog["address"] = std::string(logEntry.address());
        jLog["topic"] = Json::Value(Json::arrayValue);
        for (const auto& topic : logEntry.topics())
        {
            jLog["topic"].append(topic.hexPrefixed());
        }
        jLog["data"] = toHexStringWithPrefix(logEntry.data());
        jResp["logEntries"].append(jLog);
    }
}


void JsonRpcImpl_2_0::toJsonResp(
    Json::Value& jResp, bcos::protocol::BlockHeader::Ptr _blockHeaderPtr)
{
    if (!_blockHeaderPtr)
    {
        return;
    }

    jResp["hash"] = toHexStringWithPrefix(_blockHeaderPtr->hash());
    jResp["version"] = _blockHeaderPtr->version();
    jResp["txsRoot"] = toHexStringWithPrefix(_blockHeaderPtr->txsRoot());
    jResp["receiptsRoot"] = toHexStringWithPrefix(_blockHeaderPtr->receiptsRoot());
    jResp["stateRoot"] = toHexStringWithPrefix(_blockHeaderPtr->stateRoot());
    jResp["number"] = _blockHeaderPtr->number();
    jResp["gasUsed"] = _blockHeaderPtr->gasUsed().str(16);
    jResp["timestamp"] = _blockHeaderPtr->timestamp();
    jResp["sealer"] = _blockHeaderPtr->sealer();
    jResp["extraData"] = toHexStringWithPrefix(_blockHeaderPtr->extraData());

    jResp["consensusWeights"] = Json::Value(Json::arrayValue);
    for (const auto& wei : _blockHeaderPtr->consensusWeights())
    {
        jResp["consensusWeights"].append(wei);
    }

    jResp["sealerList"] = Json::Value(Json::arrayValue);
    for (const auto& sealer : _blockHeaderPtr->sealerList())
    {
        jResp["sealerList"].append(toHexStringWithPrefix(sealer));
    }

    Json::Value jParentInfo(Json::arrayValue);
    for (const auto& p : _blockHeaderPtr->parentInfo())
    {
        Json::Value jp;
        jp["blockNumber"] = p.blockNumber;
        jp["blockHash"] = toHexStringWithPrefix(p.blockHash);
        jParentInfo.append(jp);
    }
    jResp["parentInfo"] = jParentInfo;

    Json::Value jSignList(Json::arrayValue);
    for (const auto& sign : _blockHeaderPtr->signatureList())
    {
        Json::Value jSign;
        jSign["sealerIndex"] = sign.index;
        jSign["signature"] = toHexStringWithPrefix(sign.signature);
        jSignList.append(jSign);
    }
    jResp["signatureList"] = jSignList;
}

void JsonRpcImpl_2_0::toJsonResp(
    Json::Value& jResp, bcos::protocol::Block::Ptr _blockPtr, bool _onlyTxHash)
{
    if (!_blockPtr)
    {
        return;
    }

    // header
    toJsonResp(jResp, _blockPtr->blockHeader());
    auto txSize = _blockPtr->transactionsSize();

    Json::Value jTxs(Json::arrayValue);
    for (std::size_t index = 0; index < txSize; ++index)
    {
        Json::Value jTx;
        if (_onlyTxHash)
        {
            // Note: should not call transactionHash for in the common cases transactionHash maybe
            // empty
            jTx = toHexStringWithPrefix(_blockPtr->transaction(index)->hash());
        }
        else
        {
            toJsonResp(jTx, _blockPtr->transaction(index));
        }
        jTxs.append(jTx);
    }

    jResp["transactions"] = jTxs;
}

void JsonRpcImpl_2_0::call(std::string const& _groupID, std::string const& _nodeName,
    const std::string& _to, const std::string& _data, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_DESC("call") << LOG_KV("to", _to) << LOG_KV("group", _groupID)
                        << LOG_KV("node", _nodeName);

    auto nodeService = getNodeService(_groupID, _nodeName, "call");
    auto transactionFactory = nodeService->blockFactory()->transactionFactory();
    auto transaction =
        transactionFactory->createTransaction(0, _to, *decodeData(_data), u256(0), 0, "", "", 0);

    nodeService->scheduler()->call(
        transaction, [_to, _respFunc](Error::Ptr&& _error,
                         protocol::TransactionReceipt::Ptr&& _transactionReceiptPtr) {
            Json::Value jResp;
            if (!_error || (_error->errorCode() == bcos::protocol::CommonError::SUCCESS))
            {
                jResp["blockNumber"] = _transactionReceiptPtr->blockNumber();
                jResp["status"] = _transactionReceiptPtr->status();
                jResp["output"] = toHexStringWithPrefix(_transactionReceiptPtr->output());
            }
            else
            {
                RPC_IMPL_LOG(ERROR)
                    << LOG_BADGE("call") << LOG_KV("to", _to)
                    << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                    << LOG_KV("errorMessage", _error ? _error->errorMessage() : "success");
            }

            _respFunc(_error, jResp);
        });
}

void JsonRpcImpl_2_0::sendTransaction(std::string const& _groupID, std::string const& _nodeName,
    const std::string& _data, bool _requireProof, RespFunc _respFunc)
{
    auto self = std::weak_ptr<JsonRpcImpl_2_0>(shared_from_this());
    auto transactionDataPtr = decodeData(_data);
    auto nodeService = getNodeService(_groupID, _nodeName, "sendTransaction");
    auto txpool = nodeService->txpool();
    checkService(txpool, "txpool");
    auto tx =
        nodeService->blockFactory()->transactionFactory()->createTransaction(*transactionDataPtr);
    auto txHash = tx->hash();  // FIXME: try pass tx to backend?
    RPC_IMPL_LOG(TRACE) << LOG_DESC("sendTransaction") << LOG_KV("group", _groupID)
                        << LOG_KV("node", _nodeName) << LOG_KV("hash", txHash.abridged());
    auto submitCallback =
        [_groupID, _requireProof, tx, transactionDataPtr, respFunc = std::move(_respFunc), txHash,
            self](Error::Ptr _error,
            bcos::protocol::TransactionSubmitResult::Ptr _transactionSubmitResult) {
            auto rpc = self.lock();
            if (!rpc)
            {
                return;
            }

            if (_error && _error->errorCode() != bcos::protocol::CommonError::SUCCESS)
            {
                RPC_IMPL_LOG(ERROR)
                    << LOG_BADGE("sendTransaction") << LOG_KV("requireProof", _requireProof)
                    << LOG_KV("hash", txHash.abridged()) << LOG_KV("code", _error->errorCode())
                    << LOG_KV("message", _error->errorMessage());
                Json::Value jResp;
                respFunc(_error, jResp);

                return;
            }

            if (_transactionSubmitResult->transactionReceipt())
            {
                // get transaction receipt
                auto txHash = _transactionSubmitResult->txHash();
                auto hexPreTxHash = txHash.hexPrefixed();

                RPC_IMPL_LOG(TRACE)
                    << LOG_BADGE("sendTransaction") << LOG_DESC("getTransactionReceipt")
                    << LOG_KV("hexPreTxHash", hexPreTxHash)
                    << LOG_KV("requireProof", _requireProof);

                Json::Value jResp;
                if (_transactionSubmitResult->status() !=
                    (int32_t)bcos::protocol::TransactionStatus::None)
                {
                    std::stringstream errorMsg;
                    errorMsg
                        << (bcos::protocol::TransactionStatus)(_transactionSubmitResult->status());
                    jResp["errorMessage"] = errorMsg.str();
                }
                toJsonResp(jResp, hexPreTxHash, _transactionSubmitResult->transactionReceipt());
                jResp["input"] = toHexStringWithPrefix(tx->input());
                jResp["to"] = string(tx->to());
                jResp["from"] = toHexStringWithPrefix(tx->sender());
                // TODO: notify transactionProof
                respFunc(nullptr, jResp);
            }
        };
    txpool->asyncSubmit(transactionDataPtr, submitCallback);
}


void JsonRpcImpl_2_0::addProofToResponse(
    Json::Value& jResp, std::string const& _key, ledger::MerkleProofPtr _merkleProofPtr)
{
    if (!_merkleProofPtr)
    {
        return;
    }

    RPC_IMPL_LOG(TRACE) << LOG_DESC("addProofToResponse") << LOG_KV("key", _key)
                        << LOG_KV("key", _key) << LOG_KV("merkleProofPtr", _merkleProofPtr->size());

    uint32_t index = 0;
    for (const auto& merkleItem : *_merkleProofPtr)
    {
        jResp[_key][index]["left"] = Json::arrayValue;
        jResp[_key][index]["right"] = Json::arrayValue;
        const auto& left = merkleItem.first;
        for (const auto& item : left)
        {
            jResp[_key][index]["left"].append(item);
        }

        const auto& right = merkleItem.second;
        for (const auto& item : right)
        {
            jResp[_key][index]["right"].append(item);
        }
        ++index;
    }
}

void JsonRpcImpl_2_0::getTransaction(std::string const& _groupID, std::string const& _nodeName,
    const std::string& _txHash, bool _requireProof, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_DESC("getTransaction") << LOG_KV("txHash", _txHash)
                        << LOG_KV("requireProof", _requireProof) << LOG_KV("group", _groupID)
                        << LOG_KV("node", _nodeName);

    bcos::crypto::HashListPtr hashListPtr = std::make_shared<bcos::crypto::HashList>();
    hashListPtr->push_back(bcos::crypto::HashType(_txHash));

    auto nodeService = getNodeService(_groupID, _nodeName, "getTransaction");
    auto ledger = nodeService->ledger();
    checkService(ledger, "ledger");
    ledger->asyncGetBatchTxsByHashList(hashListPtr, _requireProof,
        [_txHash, _requireProof, _respFunc](Error::Ptr _error,
            bcos::protocol::TransactionsPtr _transactionsPtr,
            std::shared_ptr<std::map<std::string, ledger::MerkleProofPtr>> _transactionProofsPtr) {
            Json::Value jResp;
            if (!_error || (_error->errorCode() == bcos::protocol::CommonError::SUCCESS))
            {
                if (!_transactionsPtr->empty())
                {
                    auto transactionPtr = (*_transactionsPtr)[0];
                    toJsonResp(jResp, transactionPtr);
                }

                RPC_IMPL_LOG(TRACE)
                    << LOG_DESC("getTransaction") << LOG_KV("txHash", _txHash)
                    << LOG_KV("requireProof", _requireProof)
                    << LOG_KV("transactionProofsPtr size",
                           (_transactionProofsPtr ? (int64_t)_transactionProofsPtr->size() : -1));


                if (_requireProof && _transactionProofsPtr && !_transactionProofsPtr->empty())
                {
                    auto transactionProofPtr = _transactionProofsPtr->begin()->second;
                    addProofToResponse(jResp, "transactionProof", transactionProofPtr);
                }
            }
            else
            {
                RPC_IMPL_LOG(ERROR)
                    << LOG_BADGE("getTransaction") << LOG_KV("txHash", _txHash)
                    << LOG_KV("requireProof", _requireProof)
                    << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                    << LOG_KV("errorMessage", _error ? _error->errorMessage() : "success");
            }

            _respFunc(_error, jResp);
        });
}

void JsonRpcImpl_2_0::getTransactionReceipt(std::string const& _groupID,
    std::string const& _nodeName, const std::string& _txHash, bool _requireProof,
    RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_DESC("getTransactionReceipt") << LOG_KV("txHash", _txHash)
                        << LOG_KV("requireProof", _requireProof) << LOG_KV("group", _groupID)
                        << LOG_KV("node", _nodeName);

    auto hash = bcos::crypto::HashType(_txHash);

    auto nodeService = getNodeService(_groupID, _nodeName, "getTransactionReceipt");
    auto ledger = nodeService->ledger();
    checkService(ledger, "ledger");
    auto self = std::weak_ptr<JsonRpcImpl_2_0>(shared_from_this());
    ledger->asyncGetTransactionReceiptByHash(hash, _requireProof,
        [_groupID, _nodeName, _txHash, hash, _requireProof, _respFunc, self](Error::Ptr _error,
            protocol::TransactionReceipt::ConstPtr _transactionReceiptPtr,
            ledger::MerkleProofPtr _merkleProofPtr) {
            auto rpc = self.lock();
            if (!rpc)
            {
                return;
            }
            Json::Value jResp;
            if (_error && (_error->errorCode() != bcos::protocol::CommonError::SUCCESS))
            {
                RPC_IMPL_LOG(ERROR)
                    << LOG_BADGE("getTransactionReceipt") << LOG_KV("txHash", _txHash)
                    << LOG_KV("requireProof", _requireProof)
                    << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                    << LOG_KV("errorMessage", _error ? _error->errorMessage() : "success");

                _respFunc(_error, jResp);
                return;
            }

            toJsonResp(jResp, hash.hexPrefixed(), _transactionReceiptPtr);

            RPC_IMPL_LOG(TRACE) << LOG_DESC("getTransactionReceipt") << LOG_KV("txHash", _txHash)
                                << LOG_KV("requireProof", _requireProof)
                                << LOG_KV("merkleProofPtr", _merkleProofPtr);

            if (_requireProof && _merkleProofPtr)
            {
                addProofToResponse(jResp, "receiptProof", _merkleProofPtr);
            }

            // fetch transaction proof
            rpc->getTransaction(_groupID, _nodeName, _txHash, _requireProof,
                [jResp, _txHash, _respFunc](bcos::Error::Ptr _error, Json::Value& _jTx) {
                    auto jRespCopy = jResp;
                    if (_error && _error->errorCode() != bcos::protocol::CommonError::SUCCESS)
                    {
                        RPC_IMPL_LOG(WARNING)
                            << LOG_BADGE("getTransactionReceipt") << LOG_DESC("getTransaction")
                            << LOG_KV("hexPreTxHash", _txHash)
                            << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                            << LOG_KV("errorMessage", _error ? _error->errorMessage() : "success");
                    }
                    jRespCopy["input"] = _jTx["input"];
                    jRespCopy["from"] = _jTx["from"];
                    jRespCopy["to"] = _jTx["to"];
                    jRespCopy["transactionProof"] = _jTx["transactionProof"];

                    _respFunc(nullptr, jRespCopy);
                });
        });
}

void JsonRpcImpl_2_0::getBlockByHash(std::string const& _groupID, std::string const& _nodeName,
    const std::string& _blockHash, bool _onlyHeader, bool _onlyTxHash, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_DESC("getBlockByHash") << LOG_KV("blockHash", _blockHash)
                        << LOG_KV("onlyHeader", _onlyHeader) << LOG_KV("onlyTxHash", _onlyTxHash)
                        << LOG_KV("group", _groupID) << LOG_KV("node", _nodeName);

    auto nodeService = getNodeService(_groupID, _nodeName, "getBlockByHash");
    auto ledger = nodeService->ledger();
    checkService(ledger, "ledger");
    auto self = std::weak_ptr<JsonRpcImpl_2_0>(shared_from_this());
    ledger->asyncGetBlockNumberByHash(bcos::crypto::HashType(_blockHash),
        [_groupID, _nodeName, _blockHash, _onlyHeader, _onlyTxHash, _respFunc, self](
            Error::Ptr _error, protocol::BlockNumber blockNumber) {
            if (!_error || _error->errorCode() == bcos::protocol::CommonError::SUCCESS)
            {
                auto rpc = self.lock();
                if (rpc)
                {
                    // call getBlockByNumber
                    return rpc->getBlockByNumber(
                        _groupID, _nodeName, blockNumber, _onlyHeader, _onlyTxHash, _respFunc);
                }
            }
            else
            {
                RPC_IMPL_LOG(ERROR)
                    << LOG_BADGE("getBlockByHash") << LOG_KV("blockHash", _blockHash)
                    << LOG_KV("onlyHeader", _onlyHeader) << LOG_KV("onlyTxHash", _onlyTxHash)
                    << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                    << LOG_KV("errorMessage", _error ? _error->errorMessage() : "success");
                Json::Value jResp;
                _respFunc(_error, jResp);
            }
        });
}

void JsonRpcImpl_2_0::getBlockByNumber(std::string const& _groupID, std::string const& _nodeName,
    int64_t _blockNumber, bool _onlyHeader, bool _onlyTxHash, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_DESC("getBlockByNumber") << LOG_KV("_blockNumber", _blockNumber)
                        << LOG_KV("onlyHeader", _onlyHeader) << LOG_KV("onlyTxHash", _onlyTxHash)
                        << LOG_KV("group", _groupID) << LOG_KV("node", _nodeName);

    auto nodeService = getNodeService(_groupID, _nodeName, "getBlockByNumber");
    auto ledger = nodeService->ledger();
    checkService(ledger, "ledger");
    ledger->asyncGetBlockDataByNumber(_blockNumber,
        _onlyHeader ? bcos::ledger::HEADER : bcos::ledger::HEADER | bcos::ledger::TRANSACTIONS,
        [_blockNumber, _onlyHeader, _onlyTxHash, _respFunc](
            Error::Ptr _error, protocol::Block::Ptr _block) {
            Json::Value jResp;
            if (_error && _error->errorCode() != bcos::protocol::CommonError::SUCCESS)
            {
                RPC_IMPL_LOG(ERROR)
                    << LOG_BADGE("getBlockByNumber") << LOG_KV("blockNumber", _blockNumber)
                    << LOG_KV("onlyHeader", _onlyHeader) << LOG_KV("onlyTxHash", _onlyTxHash)
                    << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                    << LOG_KV("errorMessage", _error ? _error->errorMessage() : "success");
            }
            else
            {
                if (_onlyHeader)
                {
                    toJsonResp(jResp, _block ? _block->blockHeader() : nullptr);
                }
                else
                {
                    toJsonResp(jResp, _block, _onlyTxHash);
                }
            }
            _respFunc(_error, jResp);
        });
}

void JsonRpcImpl_2_0::getBlockHashByNumber(std::string const& _groupID,
    std::string const& _nodeName, int64_t _blockNumber, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_DESC("getBlockHashByNumber") << LOG_KV("blockNumber", _blockNumber)
                        << LOG_KV("group", _groupID) << LOG_KV("node", _nodeName);

    auto nodeService = getNodeService(_groupID, _nodeName, "getBlockHashByNumber");
    auto ledger = nodeService->ledger();
    checkService(ledger, "ledger");
    ledger->asyncGetBlockHashByNumber(
        _blockNumber, [_respFunc](Error::Ptr _error, crypto::HashType const& _hashValue) {
            if (_error && (_error->errorCode() != bcos::protocol::CommonError::SUCCESS))
            {
                RPC_IMPL_LOG(ERROR)
                    << LOG_BADGE("getBlockHashByNumber")
                    << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                    << LOG_KV("errorMessage", _error ? _error->errorMessage() : "success");
            }

            Json::Value jResp = _hashValue.hexPrefixed();
            _respFunc(nullptr, jResp);
        });
}

void JsonRpcImpl_2_0::getBlockNumber(
    std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_BADGE("getBlockNumber") << LOG_KV("group", _groupID)
                        << LOG_KV("node", _nodeName);

    auto nodeService = getNodeService(_groupID, _nodeName, "getBlockNumber");
    auto ledger = nodeService->ledger();
    checkService(ledger, "ledger");
    ledger->asyncGetBlockNumber([_respFunc](Error::Ptr _error, protocol::BlockNumber _blockNumber) {
        if (_error && (_error->errorCode() != bcos::protocol::CommonError::SUCCESS))
        {
            RPC_IMPL_LOG(ERROR) << LOG_BADGE("getBlockNumber")
                                << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                                << LOG_KV(
                                       "errorMessage", _error ? _error->errorMessage() : "success")
                                << LOG_KV("blockNumber", _blockNumber);
        }

        Json::Value jResp = _blockNumber;
        _respFunc(_error, jResp);
    });
}

void JsonRpcImpl_2_0::getCode(std::string const& _groupID, std::string const& _nodeName,
    const std::string _contractAddress, RespFunc _callback)
{
    RPC_IMPL_LOG(TRACE) << LOG_BADGE("getCode") << LOG_KV("contractAddress", _contractAddress)
                        << LOG_KV("group", _groupID) << LOG_KV("node", _nodeName);

    auto nodeService = getNodeService(_groupID, _nodeName, "getCode");

    auto scheduler = nodeService->scheduler();
    scheduler->getCode(
        std::string_view(_contractAddress), [_contractAddress, callback = std::move(_callback)](
                                                Error::Ptr _error, bcos::bytes _codeData) {
            std::string code;
            if (!_error || (_error->errorCode() == bcos::protocol::CommonError::SUCCESS))
            {
                if (!_codeData.empty())
                {
                    code = toHexStringWithPrefix(
                        bcos::bytesConstRef(_codeData.data(), _codeData.size()));
                }
            }
            else
            {
                RPC_IMPL_LOG(ERROR)
                    << LOG_BADGE("getCode") << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                    << LOG_KV("errorMessage", _error ? _error->errorMessage() : "success")
                    << LOG_KV("contractAddress", _contractAddress);
            }

            Json::Value jResp = code;
            callback(_error, jResp);
        });
}

void JsonRpcImpl_2_0::getSealerList(
    std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_BADGE("getSealerList") << LOG_KV("group", _groupID)
                        << LOG_KV("node", _nodeName);


    auto nodeService = getNodeService(_groupID, _nodeName, "getSealerList");
    auto ledger = nodeService->ledger();
    checkService(ledger, "ledger");
    ledger->asyncGetNodeListByType(bcos::ledger::CONSENSUS_SEALER,
        [_respFunc](Error::Ptr _error, consensus::ConsensusNodeListPtr _consensusNodeListPtr) {
            Json::Value jResp = Json::Value(Json::arrayValue);
            if (!_error || (_error->errorCode() == bcos::protocol::CommonError::SUCCESS))
            {
                if (_consensusNodeListPtr)
                {
                    for (const auto& consensusNodePtr : *_consensusNodeListPtr)
                    {
                        Json::Value node;
                        node["nodeID"] = consensusNodePtr->nodeID()->hex();
                        node["weight"] = consensusNodePtr->weight();
                        jResp.append(node);
                    }
                }
            }
            else
            {
                RPC_IMPL_LOG(ERROR)
                    << LOG_BADGE("getSealerList")
                    << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                    << LOG_KV("errorMessage", _error ? _error->errorMessage() : "success");
            }

            _respFunc(_error, jResp);
        });
}

void JsonRpcImpl_2_0::getObserverList(
    std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_BADGE("getObserverList") << LOG_KV("group", _groupID)
                        << LOG_KV("group", _groupID) << LOG_KV("node", _nodeName);

    auto nodeService = getNodeService(_groupID, _nodeName, "getObserverList");
    auto ledger = nodeService->ledger();
    checkService(ledger, "ledger");
    ledger->asyncGetNodeListByType(bcos::ledger::CONSENSUS_OBSERVER,
        [_respFunc](Error::Ptr _error, consensus::ConsensusNodeListPtr _consensusNodeListPtr) {
            Json::Value jResp = Json::Value(Json::arrayValue);
            if (!_error || (_error->errorCode() == bcos::protocol::CommonError::SUCCESS))
            {
                if (_consensusNodeListPtr)
                {
                    for (const auto& consensusNodePtr : *_consensusNodeListPtr)
                    {
                        jResp.append(consensusNodePtr->nodeID()->hex());
                    }
                }
            }
            else
            {
                RPC_IMPL_LOG(ERROR)
                    << LOG_BADGE("getObserverList")
                    << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                    << LOG_KV("errorMessage", _error ? _error->errorMessage() : "success");
            }

            _respFunc(_error, jResp);
        });
}

void JsonRpcImpl_2_0::getPbftView(
    std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_BADGE("getPbftView") << LOG_KV("group", _groupID)
                        << LOG_KV("node", _nodeName);

    auto nodeService = getNodeService(_groupID, _nodeName, "getPbftView");
    auto consensus = nodeService->consensus();
    checkService(consensus, "consensus");
    consensus->asyncGetPBFTView(
        [_respFunc](Error::Ptr _error, bcos::consensus::ViewType _viewValue) {
            Json::Value jResp;
            if (!_error || (_error->errorCode() == bcos::protocol::CommonError::SUCCESS))
            {
                jResp = _viewValue;
            }
            else
            {
                RPC_IMPL_LOG(ERROR)
                    << LOG_BADGE("getPbftView")
                    << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                    << LOG_KV("errorMessage", _error ? _error->errorMessage() : "success");
            }

            _respFunc(_error, jResp);
        });
}

void JsonRpcImpl_2_0::getPendingTxSize(
    std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_BADGE("getPendingTxSize") << LOG_KV("group", _groupID)
                        << LOG_KV("node", _nodeName);

    auto nodeService = getNodeService(_groupID, _nodeName, "getPendingTxSize");
    auto txpool = nodeService->txpool();
    checkService(txpool, "txpool");
    txpool->asyncGetPendingTransactionSize([_respFunc](Error::Ptr _error, size_t _pendingTxSize) {
        Json::Value jResp;
        if (!_error || (_error->errorCode() == bcos::protocol::CommonError::SUCCESS))
        {
            jResp = (int64_t)_pendingTxSize;
        }
        else
        {
            RPC_IMPL_LOG(ERROR) << LOG_BADGE("getPendingTxSize")
                                << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                                << LOG_KV(
                                       "errorMessage", _error ? _error->errorMessage() : "success");
        }

        _respFunc(_error, jResp);
    });
}

void JsonRpcImpl_2_0::getSyncStatus(
    std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_BADGE("getSyncStatus") << LOG_KV("group", _groupID)
                        << LOG_KV("node", _nodeName);

    auto nodeService = getNodeService(_groupID, _nodeName, "getSyncStatus");
    auto sync = nodeService->sync();
    checkService(sync, "sync");
    sync->asyncGetSyncInfo([_respFunc](Error::Ptr _error, std::string _syncStatus) {
        Json::Value jResp;
        if (!_error || (_error->errorCode() == bcos::protocol::CommonError::SUCCESS))
        {
            jResp = _syncStatus;
        }
        else
        {
            RPC_IMPL_LOG(ERROR) << LOG_BADGE("getSyncStatus")
                                << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                                << LOG_KV(
                                       "errorMessage", _error ? _error->errorMessage() : "success");
        }
        _respFunc(_error, jResp);
    });
}

void JsonRpcImpl_2_0::getConsensusStatus(
    std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_BADGE("getConsensusStatus") << LOG_KV("group", _groupID)
                        << LOG_KV("node", _nodeName);

    auto nodeService = getNodeService(_groupID, _nodeName, "getConsensusStatus");
    auto consensus = nodeService->consensus();
    checkService(consensus, "consensus");
    consensus->asyncGetConsensusStatus(
        [_respFunc](Error::Ptr _error, std::string _consensusStatus) {
            Json::Value jResp;
            if (!_error || (_error->errorCode() == bcos::protocol::CommonError::SUCCESS))
            {
                jResp = _consensusStatus;
            }
            else
            {
                RPC_IMPL_LOG(ERROR)
                    << LOG_BADGE("getConsensusStatus")
                    << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                    << LOG_KV("errorMessage", _error ? _error->errorMessage() : "success");
            }
            _respFunc(_error, jResp);
        });
}

void JsonRpcImpl_2_0::getSystemConfigByKey(std::string const& _groupID,
    std::string const& _nodeName, const std::string& _keyValue, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_DESC("getSystemConfigByKey") << LOG_KV("keyValue", _keyValue)
                        << LOG_KV("group", _groupID) << LOG_KV("node", _nodeName);

    auto nodeService = getNodeService(_groupID, _nodeName, "getSystemConfigByKey");
    auto ledger = nodeService->ledger();
    checkService(ledger, "ledger");
    ledger->asyncGetSystemConfigByKey(_keyValue,
        [_respFunc](Error::Ptr _error, std::string _value, protocol::BlockNumber _blockNumber) {
            Json::Value jResp;
            if (!_error || (_error->errorCode() == bcos::protocol::CommonError::SUCCESS))
            {
                jResp["blockNumber"] = _blockNumber;
                jResp["value"] = _value;
            }
            else
            {
                RPC_IMPL_LOG(ERROR)
                    << LOG_BADGE("asyncGetSystemConfigByKey")
                    << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                    << LOG_KV("errorMessage", _error ? _error->errorMessage() : "success");
            }

            _respFunc(_error, jResp);
        });
}

void JsonRpcImpl_2_0::getTotalTransactionCount(
    std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_DESC("getTotalTransactionCount") << LOG_KV("group", _groupID)
                        << LOG_KV("node", _nodeName);

    auto nodeService = getNodeService(_groupID, _nodeName, "getTotalTransactionCount");
    auto ledger = nodeService->ledger();
    checkService(ledger, "ledger");
    ledger->asyncGetTotalTransactionCount(
        [_respFunc](Error::Ptr _error, int64_t _totalTxCount, int64_t _failedTxCount,
            protocol::BlockNumber _latestBlockNumber) {
            Json::Value jResp;
            if (!_error || (_error->errorCode() == bcos::protocol::CommonError::SUCCESS))
            {
                jResp["blockNumber"] = _latestBlockNumber;
                jResp["transactionCount"] = _totalTxCount;
                jResp["failedTransactionCount"] = _failedTxCount;
            }
            else
            {
                RPC_IMPL_LOG(ERROR)
                    << LOG_BADGE("getTotalTransactionCount")
                    << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                    << LOG_KV("errorMessage", _error ? _error->errorMessage() : "success");
            }

            _respFunc(_error, jResp);
        });
}
void JsonRpcImpl_2_0::getPeers(RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_DESC("getPeers");
    m_gatewayInterface->asyncGetPeers(
        [this, _respFunc](Error::Ptr _error, bcos::gateway::GatewayInfo::Ptr _localP2pInfo,
            bcos::gateway::GatewayInfosPtr _peersInfo) {
            Json::Value jResp;
            if (!_error || (_error->errorCode() == bcos::protocol::CommonError::SUCCESS))
            {
                gatewayInfoToJson(jResp, _localP2pInfo, _peersInfo);
            }
            else
            {
                RPC_IMPL_LOG(ERROR)
                    << LOG_BADGE("getPeers")
                    << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                    << LOG_KV("errorMessage", _error ? _error->errorMessage() : "success");
            }

            _respFunc(_error, jResp);
        });
}

NodeService::Ptr JsonRpcImpl_2_0::getNodeService(
    std::string const& _groupID, std::string const& _nodeName, std::string const& _command)
{
    auto nodeService = m_groupManager->getNodeService(_groupID, _nodeName);
    if (!nodeService)
    {
        std::stringstream errorMsg;
        errorMsg << LOG_DESC(
                        "Invalid request for the specified node of doesn't exist or doesn't "
                        "started!")
                 << LOG_KV("request", _command) << LOG_KV("chain", m_groupManager->chainID())
                 << LOG_KV("group", _groupID) << LOG_KV("node", _nodeName);
        RPC_IMPL_LOG(WARNING) << errorMsg.str();
        BOOST_THROW_EXCEPTION(
            JsonRpcException(JsonRpcError::NodeNotExistOrNotStarted, errorMsg.str()));
    }
    return nodeService;
}

// get all the groupID list
void JsonRpcImpl_2_0::getGroupList(RespFunc _respFunc)
{
    auto response = generateResponse(nullptr);
    auto groupList = m_groupManager->groupList();
    for (auto const& it : groupList)
    {
        response["groupList"].append(it);
    }
    _respFunc(nullptr, response);
}

// get the group information of the given group
void JsonRpcImpl_2_0::getGroupInfo(std::string const& _groupID, RespFunc _respFunc)
{
    auto groupInfo = m_groupManager->getGroupInfo(_groupID);
    Json::Value response;
    if (groupInfo)
    {
        // can only recover the deleted group
        groupInfoToJson(response, groupInfo);
    }
    _respFunc(nullptr, response);
}

// get all the group info list
void JsonRpcImpl_2_0::getGroupInfoList(RespFunc _respFunc)
{
    auto groupInfoList = m_groupManager->groupInfoList();
    Json::Value response(Json::arrayValue);
    groupInfoListToJson(response, groupInfoList);
    _respFunc(nullptr, response);
}

// get the information of a given node
void JsonRpcImpl_2_0::getGroupNodeInfo(
    std::string const& _groupID, std::string const& _nodeName, RespFunc _respFunc)
{
    auto nodeInfo = m_groupManager->getNodeInfo(_groupID, _nodeName);
    Json::Value response;
    // hit the cache, response directly
    if (nodeInfo)
    {
        nodeInfoToJson(response, nodeInfo);
    }
    _respFunc(nullptr, response);
}
void JsonRpcImpl_2_0::gatewayInfoToJson(
    Json::Value& _response, bcos::gateway::GatewayInfo::Ptr _gatewayInfo)
{
    auto p2pInfo = _gatewayInfo->p2pInfo();
    _response["p2pNodeID"] = p2pInfo.p2pID;
    _response["endPoint"] =
        p2pInfo.nodeIPEndpoint.address() + ":" + std::to_string(p2pInfo.nodeIPEndpoint.port());
    // set the groupNodeIDInfo
    auto groupNodeIDInfo = _gatewayInfo->nodeIDInfo();
    Json::Value groupInfo(Json::arrayValue);
    for (auto const& it : groupNodeIDInfo)
    {
        Json::Value item;
        item["group"] = it.first;
        auto const& nodeIDList = it.second;
        Json::Value nodeIDInfo(Json::arrayValue);
        for (auto const& nodeID : nodeIDList)
        {
            nodeIDInfo.append(Json::Value(nodeID));
        }
        item["nodeIDList"] = nodeIDInfo;
        groupInfo.append(item);
    }
    _response["groupNodeIDInfo"] = groupInfo;
}

void JsonRpcImpl_2_0::gatewayInfoToJson(Json::Value& _response,
    bcos::gateway::GatewayInfo::Ptr _localP2pInfo, bcos::gateway::GatewayInfosPtr _peersInfo)
{
    if (_localP2pInfo)
    {
        gatewayInfoToJson(_response, _localP2pInfo);
    }
    if (!_peersInfo)
    {
        return;
    }
    Json::Value peersInfo(Json::arrayValue);
    for (auto const& it : *_peersInfo)
    {
        Json::Value peerInfo;
        gatewayInfoToJson(peerInfo, it);
        peersInfo.append(peerInfo);
    }
    _response["peers"] = peersInfo;
}

void JsonRpcImpl_2_0::getGroupPeers(Json::Value& _response, std::string const& _groupID,
    bcos::gateway::GatewayInfo::Ptr _localP2pInfo, bcos::gateway::GatewayInfosPtr _peersInfo)
{
    _peersInfo->emplace_back(_localP2pInfo);
    std::set<std::string> nodeIDList;
    for (auto const& info : *_peersInfo)
    {
        auto groupNodeIDInfo = info->nodeIDInfo();
        if (groupNodeIDInfo.count(_groupID))
        {
            auto const& nodeList = groupNodeIDInfo.at(_groupID);
            for (auto const& node : nodeList)
            {
                nodeIDList.insert(node);
            }
        }
    }
    if (nodeIDList.size() == 0)
    {
        return;
    }
    for (auto const& nodeID : nodeIDList)
    {
        _response.append((Json::Value)(nodeID));
    }
}

void JsonRpcImpl_2_0::getGroupPeers(std::string const& _groupID, RespFunc _respFunc)
{
    m_gatewayInterface->asyncGetPeers([_respFunc, _groupID, this](Error::Ptr _error,
                                          bcos::gateway::GatewayInfo::Ptr _localP2pInfo,
                                          bcos::gateway::GatewayInfosPtr _peersInfo) {
        Json::Value jResp;
        if (_error)
        {
            RPC_IMPL_LOG(ERROR) << LOG_BADGE("getGroupPeers") << LOG_KV("code", _error->errorCode())
                                << LOG_KV("message", _error->errorMessage());
            _respFunc(_error, jResp);
            return;
        }
        getGroupPeers(jResp, _groupID, _localP2pInfo, _peersInfo);
        _respFunc(_error, jResp);
    });
}
