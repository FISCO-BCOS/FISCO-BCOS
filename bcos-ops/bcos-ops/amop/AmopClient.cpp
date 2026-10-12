/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file AmopClient.cpp
 */
#include "AmopClient.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/RpcClient.h"
#include <bcos-cpp-sdk/Sdk.h>
#include <chrono>
#include <future>

namespace bcos::ops
{
namespace
{
class SdkAmopTransport : public AmopTransport
{
public:
    explicit SdkAmopTransport(std::shared_ptr<bcos::cppsdk::Sdk> _sdk) : m_sdk(std::move(_sdk)) {}

    void subscribe(std::string const& _topic, std::function<void(AmopMessage)> _onMessage) override
    {
        auto amop = m_sdk->amop();
        amop->subscribe(_topic,
            [_topic, _onMessage, amop](bcos::Error::Ptr _error, std::string const& _endpoint,
                std::string const& _seq, bytesConstRef _data, auto /*session*/) {
                if (_error && _error->errorCode() != 0)
                {
                    return;
                }
                AmopMessage message;
                message.topic = _topic;
                message.from = _endpoint;
                message.data.assign(_data.begin(), _data.end());
                message.reply = [amop, _endpoint, _seq](bytesConstRef _reply) {
                    amop->sendResponse(_endpoint, _seq, _reply);
                };
                _onMessage(std::move(message));
            });
    }

    AmopPublishResult publish(
        std::string const& _topic, bytesConstRef _data, int _timeoutMs) override
    {
        auto promise = std::make_shared<std::promise<AmopPublishResult>>();
        auto future = promise->get_future();
        m_sdk->amop()->publish(_topic, _data, static_cast<uint32_t>(_timeoutMs),
            [promise](bcos::Error::Ptr _error,
                std::shared_ptr<bcos::boostssl::ws::WsMessage> _message, auto /*session*/) {
                AmopPublishResult result;
                if (_error && _error->errorCode() != 0)
                {
                    result.code = _error->errorCode();
                    result.message = _error->errorMessage();
                }
                else
                {
                    result.ok = true;
                    if (_message)
                    {
                        auto payload = _message->payload();
                        result.response.assign(payload.begin(), payload.end());
                    }
                }
                try
                {
                    promise->set_value(std::move(result));
                }
                catch (std::future_error const&)
                {}
            });
        if (future.wait_for(std::chrono::milliseconds(_timeoutMs + 500)) !=
            std::future_status::ready)
        {
            AmopPublishResult timedOut;
            timedOut.code = -1;
            timedOut.message = "publish timed out after " + std::to_string(_timeoutMs) + " ms";
            return timedOut;
        }
        return future.get();
    }

private:
    std::shared_ptr<bcos::cppsdk::Sdk> m_sdk;
};
}  // namespace

std::unique_ptr<AmopTransport> makeAmopTransport(Connection const& _connection)
{
    auto sdk = sdkOf(_connection);
    if (!sdk)
    {
        throw OpsError(c_exitUsage, "amop needs the node's WebSocket RPC (source=" +
                                        _connection.source + " has no AMOP channel)");
    }
    return std::make_unique<SdkAmopTransport>(std::move(sdk));
}
}  // namespace bcos::ops
