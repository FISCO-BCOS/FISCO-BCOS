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
#include "bcos-crypto/interfaces/crypto/CommonType.h"
#include <bcos-boostssl/websocket/WsMessage.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-framework/Common.h>
#include <bcos-framework/protocol/LogEntry.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-protocol/TransactionStatus.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/jsonrpc/JsonRpcImpl_2_0.h>
#include <bcos-utilities/Base64.h>
#include <boost/algorithm/hex.hpp>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <iterator>
#include <string>
#include <string_view>

using namespace std;
using namespace bcos;
using namespace bcos::rpc;
using namespace bcos::group;
using namespace boost::iterators;
using namespace boost::archive::iterators;

JsonRpcImpl_2_0::JsonRpcImpl_2_0(GroupManager::Ptr _groupManager,
    bcos::gateway::GatewayInterface::Ptr _gatewayInterface,
    std::shared_ptr<boostssl::ws::WsService> _wsService)
  : m_groupManager(_groupManager), m_gatewayInterface(_gatewayInterface), m_wsService(_wsService)
{
    m_wsService->registerMsgHandler(bcos::protocol::MessageType::RPC_REQUEST,
        boost::bind(&JsonRpcImpl_2_0::handleRpcRequest, this, boost::placeholders::_1,
            boost::placeholders::_2));
}

void JsonRpcImpl_2_0::handleRpcRequest(
    std::shared_ptr<boostssl::MessageFace> _msg, std::shared_ptr<boostssl::ws::WsSession> _session)
{
    auto buffer = _msg->payload();
    auto req = std::string_view((const char*)buffer->data(), buffer->size());

    onRPCRequest(req, [m_buffer = std::move(buffer), _msg, _session](bcos::bytes resp) {
        if (_session && _session->isConnected())
        {
            // TODO: no need to cppy resp
            auto buffer = std::make_shared<bcos::bytes>(std::move(resp));
            _msg->setPayload(buffer);
            _session->asyncSendMessage(_msg);
        }
        else
        {
            // remove the callback
            BCOS_LOG(WARNING)
                << LOG_DESC("[RPC][FACTORY][buildJsonRpc]")
                << LOG_DESC("unable to send response for session has been inactive")
                << LOG_KV("req", std::string_view((const char*)m_buffer->data(), m_buffer->size()))
                << LOG_KV("resp", std::string_view((const char*)resp.data(), resp.size()))
                << LOG_KV("seq", _msg->seq())
                << LOG_KV("endpoint", _session ? _session->endPoint() : std::string(""));
        }
    });
}

bcos::bytes JsonRpcImpl_2_0::decodeData(std::string_view _data)
{
    bcos::bytes data;
    data.reserve(_data.size() * 2);
    boost::algorithm::hex_lower(_data, std::back_inserter(data));
    return data;
}

void JsonRpcImpl_2_0::parseRpcResponseJson(
    std::string_view _responseBody, JsonResponse& _jsonResponse)
{
    Json::Value root;
    Json::Reader jsonReader;
    std::string errorMessage;

    try
    {
        do
        {
            if (!jsonReader.parse(_responseBody.begin(), _responseBody.end(), root))
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
    // the abi
    jResp["abi"] = std::string(_transactionPtr->abi());
    // the signature
    jResp["signature"] = toHexStringWithPrefix(_transactionPtr->signatureData());
}

void JsonRpcImpl_2_0::toJsonResp(Json::Value& jResp, std::string_view _txHash,
    bcos::protocol::TransactionReceipt::ConstPtr _transactionReceiptPtr)
{
    jResp["version"] = _transactionReceiptPtr->version();
    jResp["contractAddress"] = string(_transactionReceiptPtr->contractAddress());
    jResp["gasUsed"] = _transactionReceiptPtr->gasUsed().str(16);
    jResp["status"] = _transactionReceiptPtr->status();
    jResp["blockNumber"] = _transactionReceiptPtr->blockNumber();
    jResp["output"] = toHexStringWithPrefix(_transactionReceiptPtr->output());
    jResp["message"] = _transactionReceiptPtr->message();
    jResp["transactionHash"] = std::string(_txHash);
    jResp["hash"] = _transactionReceiptPtr->hash().hexPrefixed();
    jResp["logEntries"] = Json::Value(Json::arrayValue);
    for (const auto& logEntry : _transactionReceiptPtr->logEntries())
    {
        Json::Value jLog;
        jLog["address"] = std::string(logEntry.address());
        jLog["topics"] = Json::Value(Json::arrayValue);
        for (const auto& topic : logEntry.topics())
        {
            jLog["topics"].append(topic.hexPrefixed());
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

void JsonRpcImpl_2_0::call(std::string_view _groupID, std::string_view _nodeName,
    std::string_view _to, std::string_view _data, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_DESC("call") << LOG_KV("to", _to) << LOG_KV("group", _groupID)
                        << LOG_KV("node", _nodeName) << LOG_KV("data", _data);

    auto nodeService = getNodeService(_groupID, _nodeName, "call");
    auto transactionFactory = nodeService->blockFactory()->transactionFactory();
    auto transaction =
        transactionFactory->createTransaction(0, _to, decodeData(_data), u256(0), 0, "", "", 0);

    nodeService->scheduler()->call(std::move(transaction),
        [m_to = std::string(_to), m_respFunc = std::move(_respFunc)](
            Error::Ptr&& _error, protocol::TransactionReceipt::Ptr&& _transactionReceiptPtr) {
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
                    << LOG_BADGE("call") << LOG_KV("to", m_to)
                    << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                    << LOG_KV("errorMessage", _error ? _error->errorMessage() : "success");
            }

            m_respFunc(_error, jResp);
        });
}

void JsonRpcImpl_2_0::sendTransaction(std::string_view _groupID, std::string_view _nodeName,
    std::string_view _data, bool _requireProof, RespFunc _respFunc)
{
    auto self = std::weak_ptr<JsonRpcImpl_2_0>(shared_from_this());
    auto transactionData = decodeData(_data);
    auto nodeService = getNodeService(_groupID, _nodeName, "sendTransaction");
    auto txpool = nodeService->txpool();
    checkService(txpool, "txpool");

    // Note: avoid call tx->sender() or tx->verify() here in case of verify the same transaction
    // more than once
    auto tx = nodeService->blockFactory()->transactionFactory()->createTransaction(
        transactionData, false);
    Json::Value jResp;
    jResp["input"] = toHexStringWithPrefix(tx->input());
    RPC_IMPL_LOG(TRACE) << LOG_DESC("sendTransaction") << LOG_KV("group", _groupID)
                        << LOG_KV("node", _nodeName);
    // Note: In order to avoid taking up too much memory at runtime, it is not recommended to pass
    // tx to lamba expressions for the tx will only be released when submitCallback is called
    auto submitCallback =
        [m_jResp = std::move(jResp), _requireProof, respFunc = std::move(_respFunc), self](
            Error::Ptr _error,
            bcos::protocol::TransactionSubmitResult::Ptr _transactionSubmitResult) mutable {
            auto rpc = self.lock();
            if (!rpc)
            {
                return;
            }

            if (_error && _error->errorCode() != bcos::protocol::CommonError::SUCCESS)
            {
                RPC_IMPL_LOG(ERROR)
                    << LOG_BADGE("sendTransaction") << LOG_KV("requireProof", _requireProof)
                    << LOG_KV("hash", _transactionSubmitResult ?
                                          _transactionSubmitResult->txHash().abridged() :
                                          "unknown")
                    << LOG_KV("code", _error->errorCode())
                    << LOG_KV("message", _error->errorMessage());

                respFunc(_error, m_jResp);

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
                if (_transactionSubmitResult->status() !=
                    (int32_t)bcos::protocol::TransactionStatus::None)
                {
                    std::stringstream errorMsg;
                    errorMsg << (bcos::protocol::TransactionStatus)(
                        _transactionSubmitResult->status());
                    m_jResp["errorMessage"] = errorMsg.str();
                }
                toJsonResp(m_jResp, hexPreTxHash, _transactionSubmitResult->transactionReceipt());
                m_jResp["to"] = string(_transactionSubmitResult->to());
                m_jResp["from"] = toHexStringWithPrefix(_transactionSubmitResult->sender());
                // TODO: notify transactionProof
                respFunc(nullptr, m_jResp);
            }
        };
    txpool->asyncSubmit(std::make_shared<bcos::bytes>(std::move(transactionData)), submitCallback);
}


void JsonRpcImpl_2_0::addProofToResponse(
    Json::Value& jResp, std::string_view _key, ledger::MerkleProofPtr _merkleProofPtr)
{
    // Nothing to do!
    // if (!_merkleProofPtr)
    // {
    //     return;
    // }

    // RPC_IMPL_LOG(TRACE) << LOG_DESC("addProofToResponse") << LOG_KV("key", _key)
    //                     << LOG_KV("key", _key) << LOG_KV("merkleProofPtr",
    //                     _merkleProofPtr->size());

    // uint32_t index = 0;
    // for (const auto& merkleItem : *_merkleProofPtr)
    // {
    //     jResp[_key][index]["left"] = Json::arrayValue;
    //     jResp[_key][index]["right"] = Json::arrayValue;
    //     const auto& left = merkleItem.first;
    //     for (const auto& item : left)
    //     {
    //         jResp[_key][index]["left"].append(item);
    //     }

    //     const auto& right = merkleItem.second;
    //     for (const auto& item : right)
    //     {
    //         jResp[_key][index]["right"].append(item);
    //     }
    //     ++index;
    // }
}

void JsonRpcImpl_2_0::getTransaction(std::string_view _groupID, std::string_view _nodeName,
    std::string_view _txHash, bool _requireProof, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_DESC("getTransaction") << LOG_KV("txHash", _txHash)
                        << LOG_KV("requireProof", _requireProof) << LOG_KV("group", _groupID)
                        << LOG_KV("node", _nodeName);

    auto hashListPtr = std::make_shared<bcos::crypto::HashList>();
    hashListPtr->push_back(bcos::crypto::HashType((bcos::byte*)_txHash.data(), _txHash.size()));

    auto nodeService = getNodeService(_groupID, _nodeName, "getTransaction");
    auto ledger = nodeService->ledger();
    checkService(ledger, "ledger");
    ledger->asyncGetBatchTxsByHashList(hashListPtr, _requireProof,
        [m_txHash = std::string(_txHash), _requireProof, m_respFunc = std::move(_respFunc)](
            Error::Ptr _error, bcos::protocol::TransactionsPtr _transactionsPtr,
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
                    << LOG_DESC("getTransaction") << LOG_KV("txHash", m_txHash)
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
                    << LOG_BADGE("getTransaction") << LOG_KV("txHash", m_txHash)
                    << LOG_KV("requireProof", _requireProof)
                    << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                    << LOG_KV("errorMessage", _error ? _error->errorMessage() : "success");
            }

            m_respFunc(_error, jResp);
        });
}

void JsonRpcImpl_2_0::getTransactionReceipt(std::string_view _groupID, std::string_view _nodeName,
    std::string_view _txHash, bool _requireProof, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_DESC("getTransactionReceipt") << LOG_KV("txHash", _txHash)
                        << LOG_KV("requireProof", _requireProof) << LOG_KV("group", _groupID)
                        << LOG_KV("node", _nodeName);

    auto hash = bcos::crypto::HashType((bcos::byte*)_txHash.data(), _txHash.size());

    auto nodeService = getNodeService(_groupID, _nodeName, "getTransactionReceipt");
    auto ledger = nodeService->ledger();
    checkService(ledger, "ledger");
    auto self = std::weak_ptr<JsonRpcImpl_2_0>(shared_from_this());
    ledger->asyncGetTransactionReceiptByHash(hash, _requireProof,
        [_groupID, _nodeName, m_txHash = std::string(_txHash), hash, _requireProof,
            m_respFunc = std::move(_respFunc),
            self](Error::Ptr _error, protocol::TransactionReceipt::ConstPtr _transactionReceiptPtr,
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
                    << LOG_BADGE("getTransactionReceipt") << LOG_KV("txHash", m_txHash)
                    << LOG_KV("requireProof", _requireProof)
                    << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                    << LOG_KV("errorMessage", _error ? _error->errorMessage() : "success");

                m_respFunc(_error, jResp);
                return;
            }

            toJsonResp(jResp, hash.hexPrefixed(), _transactionReceiptPtr);

            RPC_IMPL_LOG(TRACE) << LOG_DESC("getTransactionReceipt") << LOG_KV("txHash", m_txHash)
                                << LOG_KV("requireProof", _requireProof)
                                << LOG_KV("merkleProofPtr", _merkleProofPtr);

            if (_requireProof && _merkleProofPtr)
            {
                addProofToResponse(jResp, "receiptProof", _merkleProofPtr);

                // fetch transaction proof
                rpc->getTransaction(_groupID, _nodeName, m_txHash, _requireProof,
                    [m_jResp = std::move(jResp), m_txHash, m_respFunc = std::move(m_respFunc)](
                        bcos::Error::Ptr _error, Json::Value& _jTx) mutable {
                        if (_error && _error->errorCode() != bcos::protocol::CommonError::SUCCESS)
                        {
                            RPC_IMPL_LOG(WARNING)
                                << LOG_BADGE("getTransactionReceipt") << LOG_DESC("getTransaction")
                                << LOG_KV("hexPreTxHash", m_txHash)
                                << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                                << LOG_KV(
                                       "errorMessage", _error ? _error->errorMessage() : "success");
                        }
                        m_jResp["input"] = _jTx["input"];
                        m_jResp["from"] = _jTx["from"];
                        m_jResp["to"] = _jTx["to"];
                        m_jResp["transactionProof"] = _jTx["transactionProof"];

                        m_respFunc(nullptr, m_jResp);
                    });
            }
            else
            {
                m_respFunc(nullptr, jResp);
            }
        });
}

void JsonRpcImpl_2_0::getBlockByHash(std::string_view _groupID, std::string_view _nodeName,
    std::string_view _blockHash, bool _onlyHeader, bool _onlyTxHash, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_DESC("getBlockByHash") << LOG_KV("blockHash", _blockHash)
                        << LOG_KV("onlyHeader", _onlyHeader) << LOG_KV("onlyTxHash", _onlyTxHash)
                        << LOG_KV("group", _groupID) << LOG_KV("node", _nodeName);

    auto nodeService = getNodeService(_groupID, _nodeName, "getBlockByHash");
    auto ledger = nodeService->ledger();
    checkService(ledger, "ledger");
    auto self = std::weak_ptr<JsonRpcImpl_2_0>(shared_from_this());
    ledger->asyncGetBlockNumberByHash(
        bcos::crypto::HashType((bcos::byte*)_blockHash.data(), _blockHash.size()),
        [m_groupID = std::string(_groupID), m_nodeName = std::string(_nodeName),
            m_blockHash = std::string(_blockHash), _onlyHeader, _onlyTxHash,
            m_respFunc = std::move(_respFunc),
            self](Error::Ptr _error, protocol::BlockNumber blockNumber) {
            if (!_error || _error->errorCode() == bcos::protocol::CommonError::SUCCESS)
            {
                auto rpc = self.lock();
                if (rpc)
                {
                    // call getBlockByNumber
                    return rpc->getBlockByNumber(m_groupID, m_nodeName, blockNumber, _onlyHeader,
                        _onlyTxHash, std::move(m_respFunc));
                }
            }
            else
            {
                RPC_IMPL_LOG(ERROR)
                    << LOG_BADGE("getBlockByHash") << LOG_KV("blockHash", m_blockHash)
                    << LOG_KV("onlyHeader", _onlyHeader) << LOG_KV("onlyTxHash", _onlyTxHash)
                    << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                    << LOG_KV("errorMessage", _error ? _error->errorMessage() : "success");
                Json::Value jResp;
                m_respFunc(_error, jResp);
            }
        });
}

void JsonRpcImpl_2_0::getBlockByNumber(std::string_view _groupID, std::string_view _nodeName,
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
        [_blockNumber, _onlyHeader, _onlyTxHash, m_respFunc = std::move(_respFunc)](
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
            m_respFunc(_error, jResp);
        });
}

void JsonRpcImpl_2_0::getBlockHashByNumber(
    std::string_view _groupID, std::string_view _nodeName, int64_t _blockNumber, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_DESC("getBlockHashByNumber") << LOG_KV("blockNumber", _blockNumber)
                        << LOG_KV("group", _groupID) << LOG_KV("node", _nodeName);

    auto nodeService = getNodeService(_groupID, _nodeName, "getBlockHashByNumber");
    auto ledger = nodeService->ledger();
    checkService(ledger, "ledger");
    ledger->asyncGetBlockHashByNumber(_blockNumber,
        [m_respFunc = std::move(_respFunc)](Error::Ptr _error, crypto::HashType const& _hashValue) {
            if (_error && (_error->errorCode() != bcos::protocol::CommonError::SUCCESS))
            {
                RPC_IMPL_LOG(ERROR)
                    << LOG_BADGE("getBlockHashByNumber")
                    << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                    << LOG_KV("errorMessage", _error ? _error->errorMessage() : "success");
            }

            Json::Value jResp = _hashValue.hexPrefixed();
            m_respFunc(nullptr, jResp);
        });
}

void JsonRpcImpl_2_0::getBlockNumber(
    std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_BADGE("getBlockNumber") << LOG_KV("group", _groupID)
                        << LOG_KV("node", _nodeName);

    auto nodeService = getNodeService(_groupID, _nodeName, "getBlockNumber");
    auto ledger = nodeService->ledger();
    checkService(ledger, "ledger");
    ledger->asyncGetBlockNumber(
        [m_respFunc = std::move(_respFunc)](Error::Ptr _error, protocol::BlockNumber _blockNumber) {
            if (_error && (_error->errorCode() != bcos::protocol::CommonError::SUCCESS))
            {
                RPC_IMPL_LOG(ERROR)
                    << LOG_BADGE("getBlockNumber")
                    << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                    << LOG_KV("errorMessage", _error ? _error->errorMessage() : "success")
                    << LOG_KV("blockNumber", _blockNumber);
            }

            Json::Value jResp = _blockNumber;
            m_respFunc(_error, jResp);
        });
}

void JsonRpcImpl_2_0::getCode(std::string_view _groupID, std::string_view _nodeName,
    std::string_view _contractAddress, RespFunc _callback)
{
    RPC_IMPL_LOG(TRACE) << LOG_BADGE("getCode") << LOG_KV("contractAddress", _contractAddress)
                        << LOG_KV("group", _groupID) << LOG_KV("node", _nodeName);

    auto nodeService = getNodeService(_groupID, _nodeName, "getCode");

    auto scheduler = nodeService->scheduler();
    scheduler->getCode(std::string_view(_contractAddress),
        [m_contractAddress = std::string(_contractAddress), callback = std::move(_callback)](
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
                    << LOG_KV("contractAddress", m_contractAddress);
            }

            Json::Value jResp = std::move(code);
            callback(_error, jResp);
        });
}

void JsonRpcImpl_2_0::getABI(std::string_view _groupID, std::string_view _nodeName,
    std::string_view _contractAddress, RespFunc _callback)
{
    RPC_IMPL_LOG(TRACE) << LOG_BADGE("getABI") << LOG_KV("contractAddress", _contractAddress)
                        << LOG_KV("group", _groupID) << LOG_KV("node", _nodeName);

    auto nodeService = getNodeService(_groupID, _nodeName, "getABI");

    auto scheduler = nodeService->scheduler();
    scheduler->getABI(std::string_view(_contractAddress),
        [m_contractAddress = std::string(_contractAddress), callback = std::move(_callback)](
            Error::Ptr _error, std::string _abi) {
            if (_error)
            {
                RPC_IMPL_LOG(ERROR)
                    << LOG_BADGE("getABI") << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                    << LOG_KV("errorMessage", _error ? _error->errorMessage() : "success")
                    << LOG_KV("contractAddress", m_contractAddress);
            }
            Json::Value jResp = std::move(_abi);
            callback(_error, jResp);
        });
}

void JsonRpcImpl_2_0::getSealerList(
    std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_BADGE("getSealerList") << LOG_KV("group", _groupID)
                        << LOG_KV("node", _nodeName);

    auto nodeService = getNodeService(_groupID, _nodeName, "getSealerList");
    auto ledger = nodeService->ledger();
    checkService(ledger, "ledger");
    ledger->asyncGetNodeListByType(
        bcos::ledger::CONSENSUS_SEALER, [m_respFunc = std::move(_respFunc)](Error::Ptr _error,
                                            consensus::ConsensusNodeListPtr _consensusNodeListPtr) {
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

            m_respFunc(_error, jResp);
        });
}

void JsonRpcImpl_2_0::getObserverList(
    std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_BADGE("getObserverList") << LOG_KV("group", _groupID)
                        << LOG_KV("group", _groupID) << LOG_KV("node", _nodeName);

    auto nodeService = getNodeService(_groupID, _nodeName, "getObserverList");
    auto ledger = nodeService->ledger();
    checkService(ledger, "ledger");
    ledger->asyncGetNodeListByType(bcos::ledger::CONSENSUS_OBSERVER,
        [m_respFunc = std::move(_respFunc)](
            Error::Ptr _error, consensus::ConsensusNodeListPtr _consensusNodeListPtr) {
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

            m_respFunc(_error, jResp);
        });
}

void JsonRpcImpl_2_0::getPbftView(
    std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_BADGE("getPbftView") << LOG_KV("group", _groupID)
                        << LOG_KV("node", _nodeName);

    auto nodeService = getNodeService(_groupID, _nodeName, "getPbftView");
    auto consensus = nodeService->consensus();
    checkService(consensus, "consensus");
    consensus->asyncGetPBFTView([m_respFunc = std::move(_respFunc)](
                                    Error::Ptr _error, bcos::consensus::ViewType _viewValue) {
        Json::Value jResp;
        if (!_error || (_error->errorCode() == bcos::protocol::CommonError::SUCCESS))
        {
            jResp = _viewValue;
        }
        else
        {
            RPC_IMPL_LOG(ERROR) << LOG_BADGE("getPbftView")
                                << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                                << LOG_KV(
                                       "errorMessage", _error ? _error->errorMessage() : "success");
        }

        m_respFunc(_error, jResp);
    });
}

void JsonRpcImpl_2_0::getPendingTxSize(
    std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_BADGE("getPendingTxSize") << LOG_KV("group", _groupID)
                        << LOG_KV("node", _nodeName);

    auto nodeService = getNodeService(_groupID, _nodeName, "getPendingTxSize");
    auto txpool = nodeService->txpool();
    checkService(txpool, "txpool");
    txpool->asyncGetPendingTransactionSize(
        [m_respFunc = std::move(_respFunc)](Error::Ptr _error, size_t _pendingTxSize) {
            Json::Value jResp;
            if (!_error || (_error->errorCode() == bcos::protocol::CommonError::SUCCESS))
            {
                jResp = (int64_t)_pendingTxSize;
            }
            else
            {
                RPC_IMPL_LOG(ERROR)
                    << LOG_BADGE("getPendingTxSize")
                    << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                    << LOG_KV("errorMessage", _error ? _error->errorMessage() : "success");
            }

            m_respFunc(_error, jResp);
        });
}

void JsonRpcImpl_2_0::getSyncStatus(
    std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_BADGE("getSyncStatus") << LOG_KV("group", _groupID)
                        << LOG_KV("node", _nodeName);

    auto nodeService = getNodeService(_groupID, _nodeName, "getSyncStatus");
    auto sync = nodeService->sync();
    checkService(sync, "sync");
    sync->asyncGetSyncInfo(
        [m_respFunc = std::move(_respFunc)](Error::Ptr _error, std::string _syncStatus) {
            Json::Value jResp;
            if (!_error || (_error->errorCode() == bcos::protocol::CommonError::SUCCESS))
            {
                jResp = _syncStatus;
            }
            else
            {
                RPC_IMPL_LOG(ERROR)
                    << LOG_BADGE("getSyncStatus")
                    << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                    << LOG_KV("errorMessage", _error ? _error->errorMessage() : "success");
            }
            m_respFunc(_error, jResp);
        });
}

void JsonRpcImpl_2_0::getConsensusStatus(
    std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_BADGE("getConsensusStatus") << LOG_KV("group", _groupID)
                        << LOG_KV("node", _nodeName);

    auto nodeService = getNodeService(_groupID, _nodeName, "getConsensusStatus");
    auto consensus = nodeService->consensus();
    checkService(consensus, "consensus");
    consensus->asyncGetConsensusStatus(
        [m_respFunc = std::move(_respFunc)](Error::Ptr _error, std::string _consensusStatus) {
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
            m_respFunc(_error, jResp);
        });
}

void JsonRpcImpl_2_0::getSystemConfigByKey(std::string_view _groupID, std::string_view _nodeName,
    std::string_view _keyValue, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_DESC("getSystemConfigByKey") << LOG_KV("keyValue", _keyValue)
                        << LOG_KV("group", _groupID) << LOG_KV("node", _nodeName);

    auto nodeService = getNodeService(_groupID, _nodeName, "getSystemConfigByKey");
    auto ledger = nodeService->ledger();
    checkService(ledger, "ledger");
    ledger->asyncGetSystemConfigByKey(
        _keyValue, [m_respFunc = std::move(_respFunc)](
                       Error::Ptr _error, std::string _value, protocol::BlockNumber _blockNumber) {
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

            m_respFunc(_error, jResp);
        });
}

void JsonRpcImpl_2_0::getTotalTransactionCount(
    std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_DESC("getTotalTransactionCount") << LOG_KV("group", _groupID)
                        << LOG_KV("node", _nodeName);

    auto nodeService = getNodeService(_groupID, _nodeName, "getTotalTransactionCount");
    auto ledger = nodeService->ledger();
    checkService(ledger, "ledger");
    ledger->asyncGetTotalTransactionCount(
        [m_respFunc = std::move(_respFunc)](Error::Ptr _error, int64_t _totalTxCount,
            int64_t _failedTxCount, protocol::BlockNumber _latestBlockNumber) {
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

            m_respFunc(_error, jResp);
        });
}
void JsonRpcImpl_2_0::getPeers(RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_DESC("getPeers");
    m_gatewayInterface->asyncGetPeers([this, m_respFunc = std::move(_respFunc)](Error::Ptr _error,
                                          bcos::gateway::GatewayInfo::Ptr _localP2pInfo,
                                          bcos::gateway::GatewayInfosPtr _peersInfo) {
        Json::Value jResp;
        if (!_error || (_error->errorCode() == bcos::protocol::CommonError::SUCCESS))
        {
            gatewayInfoToJson(jResp, _localP2pInfo, _peersInfo);
        }
        else
        {
            RPC_IMPL_LOG(ERROR) << LOG_BADGE("getPeers")
                                << LOG_KV("errorCode", _error ? _error->errorCode() : 0)
                                << LOG_KV(
                                       "errorMessage", _error ? _error->errorMessage() : "success");
        }

        m_respFunc(_error, jResp);
    });
}

NodeService::Ptr JsonRpcImpl_2_0::getNodeService(
    std::string_view _groupID, std::string_view _nodeName, std::string_view _command)
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
void JsonRpcImpl_2_0::getGroupInfo(std::string_view _groupID, RespFunc _respFunc)
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

void JsonRpcImpl_2_0::getGroupBlockNumber(RespFunc _respFunc)
{
    Json::Value response(Json::arrayValue);
    auto groupInfoList = m_groupManager->groupInfoList();
    for (auto groupInfo : groupInfoList)
    {
        auto blockNumber = m_groupManager->getBlockNumberByGroup(groupInfo->groupID());
        if (blockNumber < 0)
        {
            RPC_IMPL_LOG(WARNING) << LOG_BADGE("getGroupBlockNumber")
                                  << LOG_DESC("getBlockNumberByGroup failed")
                                  << LOG_KV("groupID", groupInfo->groupID());
            continue;
        }

        Json::Value jValue;
        jValue[groupInfo->groupID()] = blockNumber;
        response.append(jValue);
    }

    _respFunc(nullptr, response);
}

// get the information of a given node
void JsonRpcImpl_2_0::getGroupNodeInfo(
    std::string_view _groupID, std::string_view _nodeName, RespFunc _respFunc)
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

void JsonRpcImpl_2_0::getGroupPeers(Json::Value& _response, std::string_view _groupID,
    bcos::gateway::GatewayInfo::Ptr _localP2pInfo, bcos::gateway::GatewayInfosPtr _peersInfo)
{
    _peersInfo->emplace_back(_localP2pInfo);
    std::set<std::string> nodeIDList;
    for (auto const& info : *_peersInfo)
    {
        auto groupNodeIDInfo = info->nodeIDInfo();
        auto it = groupNodeIDInfo.find(_groupID);
        if (it != groupNodeIDInfo.end())
        {
            auto const& nodeList = it->second;
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

void JsonRpcImpl_2_0::getGroupPeers(std::string_view _groupID, RespFunc _respFunc)
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