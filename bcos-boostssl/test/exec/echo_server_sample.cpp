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
 * @file echo_server_client.cpp
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
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

void usage()
{
    std::cerr << "Usage: echo-server-sample <listenIP> <listenPort> <ssl>\n"
              << "Example:\n"
              << "    ./echo-server-sample 127.0.0.1 20200 true\n"
              << "    ./echo-server-sample 127.0.0.1 20200 false\n";
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

    BCOS_LOG(INFO) << LOG_DESC("echo-server-sample") << LOG_KV("ip", host) << LOG_KV("port", port)
                   << LOG_KV("disableSsl", disableSsl);

    auto config = std::make_shared<WsConfig>();
    config->setModel(WsModel::Server);

    config->setListenIP(host);
    config->setListenPort(port);
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

    wsService->registerMsgHandler(
        999, [](std::shared_ptr<WsMessage> _msg, std::shared_ptr<WsSession> _session) {
            auto seq = std::string(_msg->seq()->begin(), _msg->seq()->end());
            auto data = std::string(_msg->data()->begin(), _msg->data()->end());
            BCOS_LOG(INFO) << LOG_BADGE(" [Main] ===>>>> ") << LOG_DESC(" receive requst seq ")
                           << LOG_KV("seq", seq);
            BCOS_LOG(INFO) << LOG_BADGE(" [Main] ===>>>> ") << LOG_DESC(" receive requst message ")
                           << LOG_KV("data", data);
            _session->asyncSendMessage(_msg);
        });

    wsService->start();

    int i = 0;
    while (true)
    {
        BCOS_LOG(INFO) << LOG_BADGE(" [Main] ===>>>> ");
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));
        i++;
    }

    return EXIT_SUCCESS;
}