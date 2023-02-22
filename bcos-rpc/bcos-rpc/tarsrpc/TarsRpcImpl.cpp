
/**
 *  Copyright (C) 2023 FISCO BCOS.
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
 * @brief impl for TarsRpcInterface
 * @file TarsRpcImpl.h
 * @author: octopus
 * @date 2023-02-22
 */

#include "bcos-rpc/tarsrpc/TarsRpcImpl.h"
#include "bcos-rpc/tarsrpc/TarsRpcInterface.h"
#include "bcos-utilities/BoostLog.h"
#include "bcos-utilities/Common.h"
#include <bcos-tars-protocol/tars/RpcProtocol.h>
#include <bcos-task/Wait.h>
#include <tup/Tars.h>
#include <exception>
#include <memory>
#include <utility>

using namespace bcos;
using namespace bcos::rpc;

TarsRpcImpl::TarsRpcImpl(std::shared_ptr<bcos::boostssl::ws::WsService> _wsService)
  : m_wsService(std::move(_wsService))
{
    m_wsService->registerMsgHandler(bcos::protocol::MessageType::TARS_SEND_TRANSACTION,
        [this](std::shared_ptr<boostssl::MessageFace> _message,
            std::shared_ptr<boostssl::ws::WsSession> _session) {
            handleRequest(std::move(_message), std::move(_session));
        });
}

void TarsRpcImpl::handleRequest(std::shared_ptr<boostssl::MessageFace> _message,
    std::shared_ptr<boostssl::ws::WsSession> _session)
{
    auto buffer = _message->payload();
    auto seq = _message->seq();
    auto endpoint = _session->endPoint();
    auto weakPtrSession = std::weak_ptr<boostssl::ws::WsSession>(_session);

    auto payload = _message->payload();

    try
    {
        // Tars decode
        tars::TarsInputStream<tars::BufferReader> input;
        input.setBuffer((const char*)payload->data(), payload->size());

        bcostars::SendTransactionRequest request;
        request.readFrom(input);

        std::string groupID = request.groupID;
        std::string nodeName = request.nodeName;
        bcos::bytes transactionData = {(bcos::byte*)request.transactionData.data(),
            (bcos::byte*)request.transactionData.data() + request.transactionData.size()};

        BCOS_LOG(DEBUG) << LOG_BADGE("TarsRpcImpl") << LOG_DESC("receive send tx request")
                        << LOG_KV("seq", _message->seq()) << LOG_KV("endpoint", endpoint)
                        << LOG_KV("groupID", groupID) << LOG_KV("nodeName", nodeName);

        auto self = weak_from_this();
        sendTransaction(groupID, nodeName, std::move(transactionData),
            [weakPtrSession, endpoint, _message, self](bcos::Error::Ptr _error,
                bcos::protocol::TransactionReceipt::Ptr _transactionReceipt) {
                auto rpc = self.lock();
                if (!rpc)
                {
                    return;
                }

                rpc->onResponse(
                    weakPtrSession, _message, std::move(_error), std::move(_transactionReceipt));
            });
    }
    catch (const std::exception& e)
    {
        BCOS_LOG(INFO) << LOG_BADGE("TarsRpcImpl")
                       << LOG_DESC("handle tars send transaction request exception")
                       << LOG_KV("seq", _message->seq()) << LOG_KV("endpoint", endpoint)
                       << LOG_KV("what", e.what());
        onResponse(weakPtrSession, _message, BCOS_ERROR_PTR(-1, e.what()), nullptr);
    }
}

void TarsRpcImpl::sendTransaction(const std::string& _groupID,  // NOLINT
    const std::string& _nodeName, bcos::bytes&& _transactionData, SendTxCallback _respCallback)
{
    auto groupManager = m_groupManager;
    task::wait([groupManager](const std::string& groupID, const std::string& nodeName,
                   bcos::bytes& transactionData, SendTxCallback callback) -> task::Task<void> {
        try
        {
            auto nodeService = getNodeService(groupID, nodeName, "sendTransaction", groupManager);
            auto txpool = nodeService->txpool();
            if (!txpool) [[unlikely]]
            {
                BOOST_THROW_EXCEPTION(
                    JsonRpcException(JsonRpcError::InternalError, "TXPool not available!"));
            }

            auto groupInfo = groupManager->getGroupInfo(groupID);
            if (!groupInfo) [[unlikely]]
            {
                BOOST_THROW_EXCEPTION(JsonRpcException(JsonRpcError::GroupNotExist,
                    "The group " + std::string(groupID) + " does not exist!"));
            }

            auto isWasm = groupInfo->wasm();
            auto transaction = nodeService->blockFactory()->transactionFactory()->createTransaction(
                bcos::ref(transactionData), false, true);

            if (c_fileLogLevel <= TRACE)
            {
                BCOS_LOG(TRACE) << LOG_BADGE("TarsRpcImpl") << LOG_DESC("sendTransaction")
                                << LOG_KV("group", groupID) << LOG_KV("node", nodeName)
                                << LOG_KV("isWasm", isWasm)
                                << LOG_KV("extraData", transaction->extraData());
            }

            std::string extraData = std::string(transaction->extraData());
            auto start = utcSteadyTime();
            co_await txpool->broadcastPushTransaction(*transaction);
            auto submitResult = co_await txpool->submitTransaction(transaction);

            auto txHash = submitResult->txHash();
            auto hexPreTxHash = txHash.hexPrefixed();
            auto status = submitResult->status();

            auto totalTime = utcSteadyTime() - start;  // ms
            if (c_fileLogLevel <= TRACE)
            {
                BCOS_LOG(TRACE) << LOG_BADGE("TarsRpcImpl") << LOG_BADGE("sendTransaction")
                                << LOG_DESC("submit callback")
                                << LOG_KV("hexPreTxHash", hexPreTxHash) << LOG_KV("status", status)
                                << LOG_KV("txCostTime", totalTime);
            }

            callback(nullptr, submitResult->transactionReceipt());
        }
        catch (bcos::Error& e)
        {
            auto info = boost::diagnostic_information(e);
            if (e.errorCode() == (int64_t)bcos::protocol::TransactionStatus::TxPoolIsFull)
            {
                BCOS_LOG(DEBUG) << LOG_BADGE("TarsRpcImpl") << "transaction pool is full"
                                << LOG_KV("errCode", e.errorCode()) << LOG_KV("msg", info);
            }
            else
            {
                BCOS_LOG(WARNING) << LOG_BADGE("TarsRpcImpl") << "sendTransaction error"
                                  << LOG_KV("errCode", e.errorCode()) << LOG_KV("msg", info);
            }

            callback(std::make_shared<bcos::Error>(std::move(e)), nullptr);
        }
        catch (std::exception& e)
        {
            auto info = boost::diagnostic_information(e);
            BCOS_LOG(WARNING) << LOG_BADGE("TarsRpcImpl") << "rpc common error: " << info;
            callback(BCOS_ERROR_PTR(-1, std::move(info)), nullptr);
        }
    }(_groupID, _nodeName, _transactionData, std::move(_respCallback)));
}

// response
void TarsRpcImpl::onResponse(std::weak_ptr<boostssl::ws::WsSession> _session,
    std::shared_ptr<boostssl::MessageFace> _message, bcos::Error::Ptr _error,
    bcos::protocol::TransactionReceipt::Ptr _transactionReceipt)
{
    auto ss = _session.lock();
    if (!ss)
    {
        return;
    }

    if (!ss->isConnected())
    {
        BCOS_LOG(TRACE) << LOG_BADGE("TarsRpcImpl")
                        << LOG_DESC("unable to send response for session has been inactive")
                        << LOG_KV("endpoint", ss->endPoint())
                        << LOG_KV("use_count", ss.use_count());
        return;
    }

    bcostars::SendTransactionResponse response;
    response.errorCode = (_error ? _error->errorCode() : 0);
    response.errorMessage = (_error ? _error->errorMessage() : std::string());
    if (_transactionReceipt)
    {
        bcos::bytes buffer;
        _transactionReceipt->encode(buffer);
        response.transactionReceiptData.insert(
            response.transactionReceiptData.end(), buffer.begin(), buffer.end());
    }

    // tars encode
    tars::TarsOutputStream<tars::BufferWriter> output;
    response.writeTo(output);

    auto buffer =
        std::make_shared<bcos::bytes>(output.getBuffer(), output.getBuffer() + output.getLength());
    _message->setPayload(buffer);
    ss->asyncSendMessage(_message);

    BCOS_LOG(TRACE) << LOG_BADGE("TarsRpcImpl") << LOG_DESC("send tx response")
                    << LOG_KV("endpoint", ss->endPoint()) << LOG_KV("seq", _message->seq())
                    << LOG_KV("data size", _message->length());
}