/*
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
 * @file echo_client_sample.cpp
 * @author: octopus
 * @date 2021-10-31
 */

#include "bcos-boostssl/websocket/WsInitializer.h"
#include <bcos-boostssl/utilities/BoostLog.h>
#include <bcos-boostssl/utilities/Common.h>
#include <bcos-boostssl/utilities/ThreadPool.h>
#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <string>

using namespace bcos;
using namespace bcos::boostssl;
using namespace bcos::boostssl::ws;
using namespace bcos::boostssl::http;
using namespace bcos::boostssl::context;
using namespace bcos::boostssl::utilities;
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

void usage()
{
    std::cerr << "Usage: echo-client-sample <peerIp> <peerPort> <ssl>\n"
              << "Example:\n"
              << "    ./echo-client-sample 127.0.0.1 20200 true\n"
              << "    ./echo-client-sample 127.0.0.1 20200 false\n";
    std::exit(0);
}


int main(int argc, char** argv)
{
    if (argc < 3)
    {
        usage();
    }

    std::string host = argv[1];
    uint16_t port = atoi(argv[2]);

    std::string disableSsl = "true";

    if (argc > 3)
    {
        disableSsl = argv[3];
    }

    BCOS_LOG(INFO) << LOG_DESC("echo-client-sample") << LOG_KV("ip", host) << LOG_KV("port", port)
                   << LOG_KV("disableSsl", disableSsl);

    auto config = std::make_shared<WsConfig>();
    config->setModel(WsModel::Client);


    EndPoint endpoint;
    endpoint.host = host;
    endpoint.port = port;

    auto peers = std::make_shared<EndPoints>();
    peers->push_back(endpoint);
    config->setConnectedPeers(peers);

    config->setThreadPoolSize(4);
    config->setDisableSsl(0 == disableSsl.compare("true"));
    if (!config->disableSsl())
    {
        auto contextConfig = std::make_shared<ContextConfig>();
        contextConfig->initConfig("./boostssl.ini");
        config->setContextConfig(contextConfig);
    }

    auto wsService = std::make_shared<ws::WsService>();
    auto wsInitializer = std::make_shared<WsInitializer>();

    wsInitializer->setConfig(config);
    wsInitializer->initWsService(wsService);

    wsService->start();

    int i = 0;
    while (true)
    {
        auto msg = wsService->messageFactory()->buildMessage();
        msg->setType(999);
        auto randStr = wsService->messageFactory()->newSeq();
        msg->setData(std::make_shared<bytes>(randStr.begin(), randStr.end()));
        BCOS_LOG(INFO) << LOG_BADGE(" [Main] ===>>>> ") << LOG_DESC("send request")
                       << LOG_KV("req", randStr);
        wsService->asyncSendMessage(msg, Options(-1),
            [](Error::Ptr _error, std::shared_ptr<WsMessage> _msg,
                std::shared_ptr<WsSession> _session) {
                (void)_session;
                if (_error && _error->errorCode() != protocol::CommonError::SUCCESS)
                {
                    BCOS_LOG(ERROR)
                        << LOG_BADGE(" [Main] ===>>>> ") << LOG_DESC("callback response error")
                        << LOG_KV("errorCode", _error->errorCode())
                        << LOG_KV("errorMessage", _error->errorMessage());
                    return;
                }

                auto resp = std::string(_msg->data()->begin(), _msg->data()->end());
                BCOS_LOG(INFO) << LOG_BADGE(" [Main] ===>>>> ") << LOG_DESC("receive response")
                               << LOG_KV("resp", resp);
            });
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));
        i++;
    }

    return EXIT_SUCCESS;
}