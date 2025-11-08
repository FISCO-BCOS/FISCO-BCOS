/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file Web3Subscribe.cpp
 * @author: octopus
 * @date 2025/08/11
 */

#include "bcos-boostssl/websocket/WsSession.h"
#include "bcos-rpc/jsonrpc/Common.h"
#include "bcos-rpc/web3jsonrpc/Web3JsonRpcImpl.h"
#include "bcos-rpc/web3jsonrpc/Web3Subscribe.h"
#include "bcos-rpc/web3jsonrpc/utils/util.h"
#include "bcos-task/Wait.h"

using namespace bcos::rpc;

/**
 * @brief onSubscribeRequest
 */
std::string Web3Subscribe::onSubscribeRequest(
    Json::Value request, std::shared_ptr<bcos::boostssl::ws::WsSession> session)
{
    std::string method = request["params"][0].asString();
    if (method == SUBSCRIBE_TYPE_NEW_HEADS)
    {
        return onSubscribeNewHeads(std::move(request), std::move(session));
    }
    else
    {
        WEB3_LOG(INFO) << LOG_BADGE("onSubscribeRequest")
                       << LOG_DESC("Unsupported eth_subscribe method") << LOG_KV("method", method)
                       << LOG_KV("request", printJson(request));
        BOOST_THROW_EXCEPTION(JsonRpcException(InvalidRequest, "Unsupported eth_subscribe method"));
    }
}

/**
 * @brief onUnsubscribeRequest
 */
bool Web3Subscribe::onUnsubscribeRequest(
    Json::Value request, std::shared_ptr<bcos::boostssl::ws::WsSession> session)
{
    std::string subscriptionId = request["params"][0].asString();
    auto endpoint = session->endPoint();

    bool unsubResult = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto result = m_newHeads2Session.erase(subscriptionId);
        auto it = m_endpoint2SubscriptionIds.find(endpoint);
        if (it != m_endpoint2SubscriptionIds.end())
        {
            it->second.erase(subscriptionId);
            if (it->second.empty())
            {
                m_endpoint2SubscriptionIds.erase(it);
            }
        }
        unsubResult = result > 0;
    }

    WEB3_LOG(INFO) << LOG_BADGE("onUnsubscribeRequest") << LOG_KV("subscriptionId", subscriptionId)
                   << LOG_KV("session", endpoint) << LOG_KV("unsub result", unsubResult);
    return unsubResult;
}

std::string Web3Subscribe::onSubscribeNewHeads(
    Json::Value request, std::shared_ptr<bcos::boostssl::ws::WsSession> session)
{
    std::string subscriptionId = generateSubscriptionId();
    auto endpoint = session->endPoint();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_newHeads2Session[subscriptionId] = std::move(session);
        m_endpoint2SubscriptionIds[endpoint].insert(subscriptionId);
    }

    WEB3_LOG(INFO) << LOG_BADGE("onSubscribeNewHeads") << LOG_KV("subscriptionId", subscriptionId)
                   << LOG_KV("session", endpoint);

    return subscriptionId;
}


void Web3Subscribe::onRemoveSubscribeBySession(
    std::shared_ptr<bcos::boostssl::ws::WsSession> session)
{
    auto endpoint = session->endPoint();

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_endpoint2SubscriptionIds.find(endpoint);
        if (it != m_endpoint2SubscriptionIds.end())
        {
            for (const auto& subscriptionId : it->second)
            {
                m_newHeads2Session.erase(subscriptionId);
            }
            m_endpoint2SubscriptionIds.erase(it);
        }
    }

    WEB3_LOG(INFO)
        << LOG_BADGE("onRemoveSubscribeBySession")
        << LOG_DESC("remove all subscriptions on this session for the session has been inactive")
        << LOG_KV("session", endpoint);
}

void Web3Subscribe::onNewBlock(int64_t blockNumber)
{
    if (c_fileLogLevel == LogLevel::TRACE) [[unlikely]]
    {
        WEB3_LOG(TRACE) << LOG_BADGE("onNewBlock") << LOG_KV("blockNumber", blockNumber);
    }

    auto web3JsonRpcImpl = m_web3JsonRpcImpl.lock();
    if (!web3JsonRpcImpl)
    {
        return;
    }

    using Sessions =
        std::vector<std::pair<std::string, std::shared_ptr<bcos::boostssl::ws::WsSession>>>;

    Sessions sessions;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& [subscriptionId, session] : m_newHeads2Session)
        {
            sessions.emplace_back(subscriptionId, session);
        }
    }

    if (sessions.empty())
    {
        return;
    }

    auto callback = [](Sessions sessions, Json::Value resp) {
        for (const auto& [subscriptionId, session] : sessions)
        {
            // modify subscriptionId
            resp["params"]["subscription"] = subscriptionId;

            auto message = session->messageFactory()->buildMessage();
            auto respBytes = toBytesResponse(resp);
            message->payload()->swap(respBytes);
            session->asyncSendMessage(std::move(message));

            if (c_fileLogLevel == LogLevel::TRACE) [[unlikely]]
            {
                WEB3_LOG(TRACE) << LOG_BADGE("onNewBlock") << LOG_DESC("send block header response")
                                << LOG_KV("subscriptionId", subscriptionId)
                                << LOG_KV("resp", printJson(resp));
            }
        }
    };

    task::wait([](int64_t blockNumber, std::weak_ptr<Web3JsonRpcImpl> weakPtrWeb3JsonRpc,
                   Sessions sessions, std::function<void(Sessions sessions, Json::Value)> callback)
                   -> task::Task<void> {
        try
        {
            auto web3JsonRpc = weakPtrWeb3JsonRpc.lock();
            if (!web3JsonRpc)
            {
                co_return;
            }

            std::stringstream ss;
            ss << std::hex << blockNumber;

            Json::Value params(Json::arrayValue);
            params.append("0x" + ss.str());
            params.append(false);

            Json::Value result;
            co_await web3JsonRpc->endpoints().getBlockByNumber(params, result);

            if (result.isMember("result") && result["result"].isMember("transactions"))
            {
                result["result"].removeMember("transactions");
            }

            Json::Value resp;
            resp["jsonrpc"] = "2.0";
            resp["method"] = "eth_subscription";
            resp["params"]["result"] = result["result"];

            if (c_fileLogLevel == LogLevel::TRACE) [[unlikely]]
            {
                WEB3_LOG(TRACE) << LOG_BADGE("onNewBlock") << LOG_DESC("getBlockByNumber success")
                                << LOG_KV("blockNumber", blockNumber)
                                << LOG_KV("response", printJson(resp));
            }

            callback(sessions, std::move(resp));
        }
        catch (...)
        {
            WEB3_LOG(ERROR) << LOG_BADGE("onNewBlock") << LOG_DESC("getBlockByNumber failed")
                            << LOG_KV("blockNumber", blockNumber)
                            << LOG_KV("error", boost::current_exception_diagnostic_information());
        }
    }(blockNumber, web3JsonRpcImpl, std::move(sessions), std::move(callback)));
}

std::string Web3Subscribe::generateSubscriptionId()
{
    std::mt19937 gen{std::random_device{}()};
    std::uniform_int_distribution<uint8_t> dis(0, 255);

    std::stringstream ss;
    ss << "0x";
    for (int i = 0; i < SUBSCRIPTION_ID_LENGTH; ++i)
    {
        uint8_t byte = dis(gen);
        ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(byte);
    }

    return ss.str();
}
