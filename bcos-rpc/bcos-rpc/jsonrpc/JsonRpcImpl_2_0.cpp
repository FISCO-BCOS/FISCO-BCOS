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
#include "bcos-crypto/ChecksumAddress.h"
#include "bcos-crypto/interfaces/crypto/CommonType.h"
#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Common.h"
#include <bcos-boostssl/websocket/WsMessage.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-framework/Common.h>
#include <bcos-framework/protocol/GlobalConfig.h>
#include <bcos-framework/protocol/LogEntry.h>
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-framework/protocol/TransactionReceipt.h>
#include <bcos-protocol/TransactionStatus.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/jsonrpc/JsonRpcImpl_2_0.h>
#include <bcos-task/Wait.h>
#include <bcos-utilities/Base64.h>
#include <json/value.h>
#include <boost/algorithm/hex.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/throw_exception.hpp>
#include <exception>
#include <iterator>
#include <stdexcept>
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
  : m_groupManager(std::move(_groupManager)),
    m_gatewayInterface(std::move(_gatewayInterface)),
    m_wsService(std::move(_wsService))
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

    auto start = std::chrono::high_resolution_clock::now();
    auto endpoint = _session->endPoint();
    auto seq = _msg->seq();
    auto version = _msg->version();
    auto ext = _msg->ext();

    auto weakptrSession = std::weak_ptr<boostssl::ws::WsSession>(_session);
    auto messageFactory = m_wsService->messageFactory();

    onRPCRequest(req, [ext, seq, version, weakptrSession, messageFactory, start](bcos::bytes resp) {
        auto session = weakptrSession.lock();

        auto end = std::chrono::high_resolution_clock::now();
        auto total = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

        if (!session)
        {
            BCOS_LOG(TRACE) << LOG_DESC("[RPC][FACTORY][buildJsonRpc]")
                            << LOG_DESC("unable to send response for session has been destroyed")
                            << LOG_KV("seq", seq) << LOG_KV("totalTime", total);
            return;
        }

        if (session->isConnected())
        {
            // TODO: no need to copy resp
            auto buffer = std::make_shared<bcos::bytes>(std::move(resp));

            auto msg = messageFactory->buildMessage();
            msg->setPayload(buffer);
            msg->setVersion(version);
            msg->setSeq(seq);
            msg->setExt(ext);
            session->asyncSendMessage(msg);
        }
        else
        {
            BCOS_LOG(TRACE) << LOG_DESC("[RPC][FACTORY][buildJsonRpc]")
                            << LOG_DESC("unable to send response for session has been inactive")
                            << LOG_KV("seq", seq) << LOG_KV("totalTime", total)
                            << LOG_KV("endpoint", session->endPoint())
                            << LOG_KV("refCount", session.use_count());
        }
    });
}

bcos::bytes JsonRpcImpl_2_0::decodeData(std::string_view _data)
{
    auto begin = _data.begin();
    auto end = _data.end();
    auto length = _data.size();

    if ((length == 0) || (length % 2 != 0)) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(std::runtime_error{"Unexpect hex string"});
    }

    if (_data.starts_with("0x"))
    {
        begin += 2;
        length -= 2;
    }

    bcos::bytes data;
    data.reserve(length / 2);
    boost::algorithm::unhex(begin, end, std::back_inserter(data));
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
                                << LOG_KV("message", _jsonResponse.error.toString())
                                << LOG_KV("responseBody", _responseBody);

            return;
        } while (0);
    }
    catch (std::exception& e)
    {
        RPC_IMPL_LOG(ERROR) << LOG_BADGE("parseRpcResponseJson")
                            << LOG_KV("response", _responseBody)
                            << LOG_KV("message", boost::diagnostic_information(e));
        BOOST_THROW_EXCEPTION(
            JsonRpcException(JsonRpcError::ParseError, "Invalid JSON was received by the server."));
    }

    RPC_IMPL_LOG(ERROR) << LOG_BADGE("parseRpcResponseJson") << LOG_KV("response", _responseBody)
                        << LOG_KV("message", errorMessage);

    BOOST_THROW_EXCEPTION(JsonRpcException(
        JsonRpcError::InvalidRequest, "The JSON sent is not a valid Response object."));
}

void bcos::rpc::toJsonResp(Json::Value& jResp, bcos::protocol::Transaction const& transaction)
{
    jResp["version"] = transaction.version();
    jResp["hash"] = toHexStringWithPrefix(transaction.hash());
    jResp["nonce"] = toHex(transaction.nonce());
    jResp["blockLimit"] = transaction.blockLimit();
    jResp["to"] = string(transaction.to());
    jResp["input"] = toHexStringWithPrefix(transaction.input());
    jResp["from"] = toHexStringWithPrefix(transaction.sender());
    jResp["importTime"] = transaction.importTime();
    jResp["chainID"] = std::string(transaction.chainId());
    jResp["groupID"] = std::string(transaction.groupId());
    jResp["abi"] = std::string(transaction.abi());
    jResp["signature"] = toHexStringWithPrefix(transaction.signatureData());
    jResp["extraData"] = std::string(transaction.extraData());
}

void bcos::rpc::toJsonResp(Json::Value& jResp, std::string_view _txHash,
    protocol::TransactionStatus status,
    bcos::protocol::TransactionReceipt const& transactionReceipt, bool _isWasm,
    crypto::Hash& hashImpl)
{
    jResp["version"] = transactionReceipt.version();
    std::string contractAddress = string(transactionReceipt.contractAddress());

    if (!contractAddress.empty() && !_isWasm)
    {
        std::string checksumContractAddr = contractAddress;
        toChecksumAddress(checksumContractAddr, hashImpl.hash(contractAddress).hex());

        if (!contractAddress.starts_with("0x") && !contractAddress.starts_with("0X"))
        {
            contractAddress = "0x" + contractAddress;
        }

        if (!checksumContractAddr.starts_with("0x") && !checksumContractAddr.starts_with("0X"))
        {
            checksumContractAddr = "0x" + checksumContractAddr;
        }

        jResp["contractAddress"] = contractAddress;
        jResp["checksumContractAddress"] = checksumContractAddr;
    }
    else
    {
        jResp["contractAddress"] = contractAddress;
        jResp["checksumContractAddress"] = contractAddress;
    }

    jResp["gasUsed"] = transactionReceipt.gasUsed().str(16);
    jResp["status"] = transactionReceipt.status();
    jResp["blockNumber"] = transactionReceipt.blockNumber();
    jResp["output"] = toHexStringWithPrefix(transactionReceipt.output());
    jResp["message"] = transactionReceipt.message();
    jResp["transactionHash"] = std::string(_txHash);

    if (status == protocol::TransactionStatus::None)
    {
        jResp["hash"] = transactionReceipt.hash().hexPrefixed();
    }
    else
    {
        jResp["hash"] = "0x";
    }

    jResp["logEntries"] = Json::Value(Json::arrayValue);
    for (const auto& logEntry : transactionReceipt.logEntries())
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


void bcos::rpc::toJsonResp(Json::Value& jResp, bcos::protocol::BlockHeader::Ptr _blockHeaderPtr)
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

void bcos::rpc::toJsonResp(Json::Value& jResp, bcos::protocol::Block& block, bool _onlyTxHash)
{
    // header
    toJsonResp(jResp, block.blockHeader());
    auto txSize = _onlyTxHash ? block.transactionsMetaDataSize() : block.transactionsSize();

    Json::Value jTxs(Json::arrayValue);
    for (std::size_t index = 0; index < txSize; ++index)
    {
        Json::Value jTx;
        if (_onlyTxHash)
        {
            // Note: should not call transactionHash for in the common cases transactionHash maybe
            // empty
            jTx = toHexStringWithPrefix(block.transactionMetaData(index)->hash());
        }
        else
        {
            auto transaction = block.transaction(index);
            toJsonResp(jTx, *transaction);
        }
        jTxs.append(jTx);
    }

    jResp["transactions"] = jTxs;
}

void JsonRpcImpl_2_0::call(std::string_view _groupID, std::string_view _nodeName,
    std::string_view _to, std::string_view _data, RespFunc _respFunc)
{
    if (c_fileLogLevel == LogLevel::TRACE) [[unlikely]]
    {
        RPC_IMPL_LOG(TRACE) << LOG_DESC("call") << LOG_KV("to", _to) << LOG_KV("group", _groupID)
                            << LOG_KV("node", _nodeName) << LOG_KV("data", _data);
    }

    auto nodeService = getNodeService(_groupID, _nodeName, "call");
    auto transactionFactory = nodeService->blockFactory()->transactionFactory();
    auto transaction = transactionFactory->createTransaction(
        0, std::string(_to), decodeData(_data), "", 0, std::string(), std::string(), 0);
    execCall(std::move(nodeService), std::move(transaction), std::move(_respFunc));
}

void JsonRpcImpl_2_0::call(std::string_view _groupID, std::string_view _nodeName,
    std::string_view _to, std::string_view _data, std::string_view _sign,
    bcos::rpc::RespFunc _respFunc)
{
    if (c_fileLogLevel == LogLevel::TRACE) [[unlikely]]
    {
        RPC_IMPL_LOG(TRACE) << LOG_DESC("call") << LOG_KV("to", _to) << LOG_KV("group", _groupID)
                            << LOG_KV("node", _nodeName) << LOG_KV("data", _data)
                            << LOG_KV("sign", _sign);
    }
    auto nodeService = getNodeService(_groupID, _nodeName, "call");
    auto groupInfo = m_groupManager->getGroupInfo(_groupID);
    // FIXME: groupInfo tars not maintenance smCryptoType
    auto cryptoType = groupInfo->smCryptoType() ? group::SM_NODE : group::NON_SM_NODE;
    auto transactionFactory = nodeService->blockFactory()->transactionFactory();
    auto transaction = transactionFactory->createTransaction(
        0, std::string(_to), decodeData(_data), "", 0, std::string(), std::string(), 0);
    auto [result, sender] = CallValidator::verify(_to, _data, _sign, cryptoType);
    if (!result) [[unlikely]]
    {
        RPC_IMPL_LOG(TRACE) << LOG_DESC("call with sign verify failed") << LOG_KV("to", _to)
                            << LOG_KV("data", _data) << LOG_KV("sign", _sign);
        Json::Value jResp;
        _respFunc(BCOS_ERROR_PTR(
                      (int32_t)protocol::TransactionStatus::InvalidSignature, "Invalid signature"),
            jResp);
        return;
    }
    transaction->forceSender(sender);
    execCall(std::move(nodeService), std::move(transaction), std::move(_respFunc));
}

void JsonRpcImpl_2_0::sendTransaction(std::string_view groupID, std::string_view nodeName,
    std::string_view data, bool requireProof, RespFunc respFunc)
{
    task::wait([](JsonRpcImpl_2_0* self, std::string_view groupID, std::string_view nodeName,
                   std::string_view data, bool requireProof,
                   RespFunc respFunc) -> task::Task<void> {
        auto nodeService = self->getNodeService(groupID, nodeName, "sendTransaction");

        auto txpool = nodeService->txpool();
        if (!txpool) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(
                JsonRpcException(JsonRpcError::InternalError, "TXPool not available!"));
        }

        auto groupInfo = self->m_groupManager->getGroupInfo(groupID);
        if (!groupInfo) [[unlikely]]
        {
            BOOST_THROW_EXCEPTION(JsonRpcException(JsonRpcError::GroupNotExist,
                "The group " + std::string(groupID) + " does not exist!"));
        }

        Json::Value jResp;
        try
        {
            auto isWasm = groupInfo->wasm();
            auto transactionData = decodeData(data);
            auto transaction = nodeService->blockFactory()->transactionFactory()->createTransaction(
                bcos::ref(transactionData), false, true);

            if (c_fileLogLevel <= TRACE)
            {
                RPC_IMPL_LOG(TRACE) << LOG_DESC("sendTransaction") << LOG_KV("group", groupID)
                                    << LOG_KV("node", nodeName) << LOG_KV("isWasm", isWasm);
            }

            auto start = utcSteadyTime();
            std::string extraData = std::string(transaction->extraData());
            txpool->broadcastTransactionBuffer(bcos::ref(transactionData));
            auto submitResult = co_await txpool->submitTransaction(transaction);

            auto txHash = submitResult->txHash();
            auto hexPreTxHash = txHash.hexPrefixed();
            auto status = submitResult->status();

            auto totalTime = utcSteadyTime() - start;  // ms
            if (c_fileLogLevel <= TRACE)
            {
                RPC_IMPL_LOG(TRACE)
                    << LOG_BADGE("sendTransaction") << LOG_DESC("submit callback")
                    << LOG_KV("hexPreTxHash", hexPreTxHash) << LOG_KV("status", status)
                    << LOG_KV("requireProof", requireProof) << LOG_KV("txCostTime", totalTime);
            }

            toJsonResp(jResp, hexPreTxHash, (protocol::TransactionStatus)submitResult->status(),
                *(submitResult->transactionReceipt()), isWasm,
                *(nodeService->blockFactory()->cryptoSuite()->hashImpl()));
            jResp["to"] = submitResult->to();
            jResp["from"] = toHexStringWithPrefix(submitResult->sender());

            if (g_BCOSConfig.needRetInput())
            {
                jResp["input"] = toHexStringWithPrefix(transaction->input());
                jResp["extraData"] = extraData;
            }


            if (requireProof) [[unlikely]]
            {
                auto ledger = nodeService->ledger();
                ledger->asyncGetTransactionReceiptByHash(txHash, true,
                    [&respFunc, &jResp](
                        auto&& error, [[maybe_unused]] auto&& receipt, auto&& merkle) {
                        // ledger logic: if error, return empty merkle
                        // for compatibility
                        JsonRpcImpl_2_0::addProofToResponse(jResp, "txReceiptProof", merkle);
                        JsonRpcImpl_2_0::addProofToResponse(jResp, "receiptProof", merkle);
                        respFunc(nullptr, jResp);
                    });
                co_return;
            }
            respFunc(nullptr, jResp);
        }
        catch (bcos::Error& e)
        {
            auto info = boost::diagnostic_information(e);
            if (e.errorCode() == (int64_t)bcos::protocol::TransactionStatus::TxPoolIsFull)
            {
                RPC_IMPL_LOG(DEBUG) << "sendTransaction error" << LOG_KV("errCode", e.errorCode())
                                    << LOG_KV("msg", info);
            }
            else
            {
                RPC_IMPL_LOG(WARNING) << "sendTransaction error" << LOG_KV("errCode", e.errorCode())
                                      << LOG_KV("msg", info);
            }
            respFunc(std::make_shared<bcos::Error>(std::move(e)), jResp);
        }
        catch (std::exception& e)
        {
            auto info = boost::diagnostic_information(e);
            RPC_IMPL_LOG(WARNING) << "RPC common error: " << info;
            respFunc(BCOS_ERROR_PTR(-1, std::move(info)), jResp);
        }
    }(this, groupID, nodeName, data, requireProof, std::move(respFunc)));
}


void JsonRpcImpl_2_0::addProofToResponse(
    Json::Value& jResp, const std::string& _key, ledger::MerkleProofPtr _merkleProofPtr)
{
    if (!_merkleProofPtr)
    {
        return;
    }

    RPC_IMPL_LOG(TRACE) << LOG_DESC("addProofToResponse") << LOG_KV("key", _key)
                        << LOG_KV("key", _key) << LOG_KV("merkleProofPtr", _merkleProofPtr->size());

    uint32_t index = 0;
    jResp[_key] = Json::arrayValue;
    std::for_each(_merkleProofPtr->begin(), _merkleProofPtr->end(),
        [&jResp, &_key](const auto& item) { jResp[_key].append(item.hex()); });
}

void JsonRpcImpl_2_0::getTransaction(std::string_view _groupID, std::string_view _nodeName,
    std::string_view _txHash, bool _requireProof, RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_DESC("getTransaction") << LOG_KV("txHash", _txHash)
                        << LOG_KV("requireProof", _requireProof) << LOG_KV("group", _groupID)
                        << LOG_KV("node", _nodeName);

    auto hashListPtr = std::make_shared<bcos::crypto::HashList>();

    // TODO: Error hash here, need hex2bin
    hashListPtr->push_back(bcos::crypto::HashType(_txHash, bcos::crypto::HashType::FromHex));

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
                    toJsonResp(jResp, *transactionPtr);
                }

                RPC_IMPL_LOG(TRACE)
                    << LOG_DESC("getTransaction") << LOG_KV("txHash", m_txHash)
                    << LOG_KV("requireProof", _requireProof)
                    << LOG_KV("transactionProofsPtr size",
                           (_transactionProofsPtr ? (int64_t)_transactionProofsPtr->size() : -1));


                if (_requireProof && _transactionProofsPtr && !_transactionProofsPtr->empty())
                {
                    auto transactionProofPtr = _transactionProofsPtr->begin()->second;
                    // for compatibility
                    addProofToResponse(
                        jResp, "transactionProof", std::make_shared<ledger::MerkleProof>());
                    addProofToResponse(jResp, "txProof", transactionProofPtr);
                }
            }
            else
            {
                RPC_IMPL_LOG(INFO)
                    << LOG_BADGE("getTransaction failed") << LOG_KV("txHash", m_txHash)
                    << LOG_KV("requireProof", _requireProof)
                    << LOG_KV("code", _error ? _error->errorCode() : 0)
                    << LOG_KV("message", _error ? _error->errorMessage() : "success");
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

    auto hash = bcos::crypto::HashType(_txHash, bcos::crypto::HashType::FromHex);

    auto nodeService = getNodeService(_groupID, _nodeName, "getTransactionReceipt");
    auto ledger = nodeService->ledger();

    checkService(ledger, "ledger");
    auto hashImpl = nodeService->blockFactory()->cryptoSuite()->hashImpl();

    auto groupInfo = m_groupManager->getGroupInfo(_groupID);
    if (!groupInfo)
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(JsonRpcError::GroupNotExist,
            "The group " + std::string(_groupID) + " does not exist!"));
    }

    bool isWasm = groupInfo->wasm();

    auto self = std::weak_ptr<JsonRpcImpl_2_0>(shared_from_this());
    ledger->asyncGetTransactionReceiptByHash(hash, _requireProof,
        [m_group = std::string(_groupID), m_nodeName = std::string(_nodeName),
            m_txHash = std::string(_txHash), hash, _requireProof, m_respFunc = std::move(_respFunc),
            self, hashImpl, isWasm](Error::Ptr _error,
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
                RPC_IMPL_LOG(INFO)
                    << LOG_BADGE("getTransactionReceipt failed") << LOG_KV("txHash", m_txHash)
                    << LOG_KV("requireProof", _requireProof)
                    << LOG_KV("code", _error ? _error->errorCode() : 0)
                    << LOG_KV("message", _error ? _error->errorMessage() : "success");

                m_respFunc(_error, jResp);
                return;
            }

            toJsonResp(jResp, hash.hexPrefixed(), protocol::TransactionStatus::None,
                *_transactionReceiptPtr, isWasm, *hashImpl);

            RPC_IMPL_LOG(TRACE) << LOG_DESC("getTransactionReceipt") << LOG_KV("txHash", m_txHash)
                                << LOG_KV("requireProof", _requireProof)
                                << LOG_KV("merkleProofPtr", _merkleProofPtr);

            if (_requireProof && _merkleProofPtr)
            {
                addProofToResponse(jResp, "receiptProof", std::make_shared<ledger::MerkleProof>());
                // for compatibility
                addProofToResponse(jResp, "txReceiptProof", _merkleProofPtr);
            }

            // fetch transaction proof
            rpc->getTransaction(m_group, m_nodeName, m_txHash, _requireProof,
                [m_jResp = std::move(jResp), m_txHash, m_respFunc = std::move(m_respFunc)](
                    bcos::Error::Ptr _error, Json::Value& _jTx) mutable {
                    if (_error && _error->errorCode() != bcos::protocol::CommonError::SUCCESS)
                    {
                        RPC_IMPL_LOG(WARNING)
                            << LOG_BADGE("getTransactionReceipt") << LOG_DESC("getTransaction")
                            << LOG_KV("hexPreTxHash", m_txHash)
                            << LOG_KV("code", _error ? _error->errorCode() : 0)
                            << LOG_KV("message", _error ? _error->errorMessage() : "success");
                    }
                    m_jResp["input"] = _jTx["input"];
                    m_jResp["from"] = _jTx["from"];
                    m_jResp["to"] = _jTx["to"];
                    m_jResp["extraData"] = _jTx["extraData"];
                    m_jResp["transactionProof"] = _jTx["transactionProof"];

                    m_respFunc(nullptr, m_jResp);
                });
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
        bcos::crypto::HashType(_blockHash, bcos::crypto::HashType::FromHex),
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
                RPC_IMPL_LOG(INFO)
                    << LOG_BADGE("getBlockByHash failed") << LOG_KV("blockHash", m_blockHash)
                    << LOG_KV("onlyHeader", _onlyHeader) << LOG_KV("onlyTxHash", _onlyTxHash)
                    << LOG_KV("code", _error ? _error->errorCode() : 0)
                    << LOG_KV("message", _error ? _error->errorMessage() : "success");
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
    auto flag = _onlyHeader ?
                    bcos::ledger::HEADER :
                    (_onlyTxHash ? bcos::ledger::HEADER | bcos::ledger::TRANSACTIONS_HASH :
                                   bcos::ledger::HEADER | bcos::ledger::TRANSACTIONS);
    ledger->asyncGetBlockDataByNumber(_blockNumber, flag,
        [_blockNumber, _onlyHeader, _onlyTxHash, m_respFunc = std::move(_respFunc)](
            Error::Ptr _error, protocol::Block::Ptr _block) {
            Json::Value jResp;
            if (_error && _error->errorCode() != bcos::protocol::CommonError::SUCCESS)
            {
                RPC_IMPL_LOG(INFO)
                    << LOG_BADGE("getBlockByNumber failed") << LOG_KV("blockNumber", _blockNumber)
                    << LOG_KV("onlyHeader", _onlyHeader) << LOG_KV("onlyTxHash", _onlyTxHash)
                    << LOG_KV("code", _error ? _error->errorCode() : 0)
                    << LOG_KV("message", _error ? _error->errorMessage() : "success");
            }
            else
            {
                if (_onlyHeader)
                {
                    toJsonResp(jResp, _block ? _block->blockHeader() : nullptr);
                }
                else
                {
                    toJsonResp(jResp, *_block, _onlyTxHash);
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
                RPC_IMPL_LOG(INFO)
                    << LOG_BADGE("getBlockHashByNumber failed")
                    << LOG_KV("code", _error ? _error->errorCode() : 0)
                    << LOG_KV("message", _error ? _error->errorMessage() : "success");
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
                RPC_IMPL_LOG(INFO) << LOG_BADGE("getBlockNumber failed")
                                   << LOG_KV("code", _error ? _error->errorCode() : 0)
                                   << LOG_KV("message", _error ? _error->errorMessage() : "success")
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

    auto groupInfo = m_groupManager->getGroupInfo(_groupID);
    if (!groupInfo) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(JsonRpcError::GroupNotExist,
            "The group " + std::string(_groupID) + " does not exist!"));
    }

    auto isWasm = groupInfo->wasm();
    // trim 0x prefix for solidity contract
    if (!isWasm && (_contractAddress.starts_with("0x") || _contractAddress.starts_with("0X")))
    {
        _contractAddress = _contractAddress.substr(2);
    }

    auto lowerAddress = boost::to_lower_copy(std::string(_contractAddress));

    auto scheduler = nodeService->scheduler();
    scheduler->getCode(std::string_view(lowerAddress),
        [lowerAddress, callback = std::move(_callback)](Error::Ptr _error, bcos::bytes _codeData) {
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
                RPC_IMPL_LOG(INFO) << LOG_BADGE("getCode failed")
                                   << LOG_KV("code", _error ? _error->errorCode() : 0)
                                   << LOG_KV("message", _error ? _error->errorMessage() : "success")
                                   << LOG_KV("contractAddress", lowerAddress);
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

    auto groupInfo = m_groupManager->getGroupInfo(_groupID);
    if (!groupInfo) [[unlikely]]
    {
        BOOST_THROW_EXCEPTION(JsonRpcException(JsonRpcError::GroupNotExist,
            "The group " + std::string(_groupID) + " does not exist!"));
    }

    auto isWasm = groupInfo->wasm();
    // trim 0x prefix for solidity contract address
    if (!isWasm && (_contractAddress.starts_with("0x") || _contractAddress.starts_with("0X")))
    {
        _contractAddress = _contractAddress.substr(2);
    }


    auto lowerAddress = boost::to_lower_copy(std::string(_contractAddress));

    auto scheduler = nodeService->scheduler();
    scheduler->getABI(std::string_view(lowerAddress),
        [lowerAddress, callback = std::move(_callback)](Error::Ptr _error, std::string _abi) {
            if (_error)
            {
                RPC_IMPL_LOG(INFO) << LOG_BADGE("getABI failed")
                                   << LOG_KV("code", _error ? _error->errorCode() : 0)
                                   << LOG_KV("message", _error ? _error->errorMessage() : "success")
                                   << LOG_KV("contractAddress", lowerAddress);
            }
            Json::Value jResp = _abi;
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
                RPC_IMPL_LOG(INFO)
                    << LOG_BADGE("getSealerList failed")
                    << LOG_KV("code", _error ? _error->errorCode() : 0)
                    << LOG_KV("message", _error ? _error->errorMessage() : "success");
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
                RPC_IMPL_LOG(INFO)
                    << LOG_BADGE("getObserverList failed")
                    << LOG_KV("code", _error ? _error->errorCode() : 0)
                    << LOG_KV("message", _error ? _error->errorMessage() : "success");
            }

            m_respFunc(_error, jResp);
        });
}

void JsonRpcImpl_2_0::getNodeListByType(std::string_view _groupID, std::string_view _nodeName,
    std::string_view _nodeType, bcos::rpc::RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_BADGE("getNodeListByType") << LOG_KV("group", _groupID)
                        << LOG_KV("group", _groupID) << LOG_KV("node", _nodeName)
                        << LOG_KV("type", _nodeType);

    auto nodeService = getNodeService(_groupID, _nodeName, "getNodeListByType");
    auto ledger = nodeService->ledger();
    checkService(ledger, "ledger");
    ledger->asyncGetNodeListByType(
        _nodeType, [m_respFunc = std::move(_respFunc)](
                       Error::Ptr _error, consensus::ConsensusNodeListPtr _consensusNodeListPtr) {
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
                RPC_IMPL_LOG(INFO)
                    << LOG_BADGE("getObserverList failed")
                    << LOG_KV("code", _error ? _error->errorCode() : 0)
                    << LOG_KV("message", _error ? _error->errorMessage() : "success");
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
            RPC_IMPL_LOG(INFO) << LOG_BADGE("getPbftView failed")
                               << LOG_KV("code", _error ? _error->errorCode() : 0)
                               << LOG_KV("message", _error ? _error->errorMessage() : "success");
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
    txpool->asyncGetPendingTransactionSize([m_respFunc = std::move(_respFunc)](
                                               Error::Ptr _error, size_t _pendingTxSize) {
        Json::Value jResp;
        if (!_error || (_error->errorCode() == bcos::protocol::CommonError::SUCCESS))
        {
            jResp = (int64_t)_pendingTxSize;
        }
        else
        {
            RPC_IMPL_LOG(INFO) << LOG_BADGE("getPendingTxSize failed")
                               << LOG_KV("code", _error ? _error->errorCode() : 0)
                               << LOG_KV("message", _error ? _error->errorMessage() : "success");
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
    sync->asyncGetSyncInfo([m_respFunc = std::move(_respFunc)](
                               Error::Ptr _error, std::string _syncStatus) {
        Json::Value jResp;
        if (!_error || (_error->errorCode() == bcos::protocol::CommonError::SUCCESS))
        {
            jResp = _syncStatus;
        }
        else
        {
            RPC_IMPL_LOG(INFO) << LOG_BADGE("getSyncStatus failed")
                               << LOG_KV("code", _error ? _error->errorCode() : 0)
                               << LOG_KV("message", _error ? _error->errorMessage() : "success");
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
    consensus->asyncGetConsensusStatus([m_respFunc = std::move(_respFunc)](
                                           Error::Ptr _error, std::string _consensusStatus) {
        Json::Value jResp;
        if (!_error || (_error->errorCode() == bcos::protocol::CommonError::SUCCESS))
        {
            jResp = _consensusStatus;
        }
        else
        {
            RPC_IMPL_LOG(INFO) << LOG_BADGE("getConsensusStatus failed")
                               << LOG_KV("code", _error ? _error->errorCode() : 0)
                               << LOG_KV("message", _error ? _error->errorMessage() : "success");
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
                RPC_IMPL_LOG(INFO)
                    << LOG_BADGE("asyncGetSystemConfigByKey")
                    << LOG_KV("code", _error ? _error->errorCode() : 0)
                    << LOG_KV("message", _error ? _error->errorMessage() : "success");
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
    ledger->asyncGetTotalTransactionCount([m_respFunc = std::move(_respFunc)](Error::Ptr _error,
                                              int64_t _totalTxCount, int64_t _failedTxCount,
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
            RPC_IMPL_LOG(INFO) << LOG_BADGE("getTotalTransactionCount failed")
                               << LOG_KV("code", _error ? _error->errorCode() : 0)
                               << LOG_KV("message", _error ? _error->errorMessage() : "success");
        }

        m_respFunc(_error, jResp);
    });
}
void JsonRpcImpl_2_0::getPeers(RespFunc _respFunc)
{
    RPC_IMPL_LOG(TRACE) << LOG_DESC("getPeers");
    auto self = std::weak_ptr<JsonRpcImpl_2_0>(shared_from_this());
    m_gatewayInterface->asyncGetPeers([m_respFunc = std::move(_respFunc), self](Error::Ptr _error,
                                          bcos::gateway::GatewayInfo::Ptr _localP2pInfo,
                                          bcos::gateway::GatewayInfosPtr _peersInfo) {
        auto rpc = self.lock();
        if (!rpc)
        {
            return;
        }
        Json::Value jResp;
        if (!_error || (_error->errorCode() == bcos::protocol::CommonError::SUCCESS))
        {
            rpc->gatewayInfoToJson(jResp, _localP2pInfo, _peersInfo);
        }
        else
        {
            RPC_IMPL_LOG(INFO) << LOG_BADGE("getPeers failed")
                               << LOG_KV("code", _error ? _error->errorCode() : 0)
                               << LOG_KV("message", _error ? _error->errorMessage() : "success");
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
    if (!p2pInfo.nodeIPEndpoint.address().empty())
    {
        _response["endPoint"] =
            p2pInfo.nodeIPEndpoint.address() + ":" + std::to_string(p2pInfo.nodeIPEndpoint.port());
    }
    // set the groupNodeIDInfo
    auto groupNodeIDInfo = _gatewayInfo->nodeIDInfo();
    Json::Value groupInfo(Json::arrayValue);
    for (auto const& it : groupNodeIDInfo)
    {
        Json::Value item;
        item["group"] = it.first;
        auto const& nodeIDInfos = it.second;
        Json::Value nodeIDInfo(Json::arrayValue);
        for (auto const& nodeInfo : nodeIDInfos)
        {
            nodeIDInfo.append(Json::Value(nodeInfo.first));
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
        for (auto const& nodeIDInfo : groupNodeIDInfo)
        {
            auto groupID = nodeIDInfo.first;

            if (_groupID != groupID)
            {
                continue;
            }

            auto nodeInfo = nodeIDInfo.second;
            for (auto const& peerInfo : nodeInfo)
            {
                nodeIDList.insert(peerInfo.first);
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
    auto self = std::weak_ptr<JsonRpcImpl_2_0>(shared_from_this());
    m_gatewayInterface->asyncGetPeers(
        [_respFunc, group = std::string(_groupID), self](Error::Ptr _error,
            bcos::gateway::GatewayInfo::Ptr _localP2pInfo,
            bcos::gateway::GatewayInfosPtr _peersInfo) {
            Json::Value jResp(Json::arrayValue);
            if (_error)
            {
                RPC_IMPL_LOG(INFO)
                    << LOG_BADGE("getGroupPeers failed") << LOG_KV("code", _error->errorCode())
                    << LOG_KV("message", _error->errorMessage());
                _respFunc(_error, jResp);
                return;
            }
            auto rpc = self.lock();
            if (!rpc)
            {
                return;
            }
            rpc->getGroupPeers(jResp, std::string_view(group), _localP2pInfo, _peersInfo);
            _respFunc(_error, jResp);
        });
}

void JsonRpcImpl_2_0::execCall(
    NodeService::Ptr nodeService, protocol::Transaction::Ptr _tx, bcos::rpc::RespFunc _respFunc)
{
    nodeService->scheduler()->call(
        _tx, [m_to = std::string(_tx->to()), m_respFunc = std::move(_respFunc)](
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
                RPC_IMPL_LOG(INFO)
                    << LOG_BADGE("call failed") << LOG_KV("to", m_to)
                    << LOG_KV("code", _error ? _error->errorCode() : 0)
                    << LOG_KV("message", _error ? _error->errorMessage() : "success");
            }
            m_respFunc(_error, jResp);
        });
}
