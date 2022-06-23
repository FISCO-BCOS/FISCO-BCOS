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
#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/ThreadPool.h>
#include <string>

using namespace bcos;
using namespace bcos::boostssl;
using namespace bcos::boostssl::ws;
using namespace bcos::boostssl::http;
using namespace bcos::boostssl::context;
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

std::string MODULE_NAME = "DEFAULT";

#define TEST_SERVER_LOG(LEVEL, MODULE_NAME) \
    BCOS_LOG(LEVEL) << LOG_BADGE(MODULE_NAME) << "[WS][SERVICE]"

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

    MODULE_NAME = "TEST_SERVER_MODULE";
    TEST_SERVER_LOG(INFO, MODULE_NAME) << LOG_DESC("echo-server-sample") << LOG_KV("ip", host)
                                       << LOG_KV("port", port) << LOG_KV("disableSsl", disableSsl);

    auto config = std::make_shared<WsConfig>();
    config->setModel(WsModel::Server);

    config->setListenIP(host);
    config->setListenPort(port);
    config->setThreadPoolSize(1);
    config->setDisableSsl(0 == disableSsl.compare("true"));
    if (!config->disableSsl())
    {
        auto contextConfig = std::make_shared<ContextConfig>();
        contextConfig->initConfig("./boostssl.ini");
        config->setContextConfig(contextConfig);
    }
    config->setModuleName("TEST_SERVER");

    auto wsService = std::make_shared<ws::WsService>(config->moduleName());
    auto wsInitializer = std::make_shared<WsInitializer>();

    auto sessionFactory = std::make_shared<WsSessionFactory>();
    wsInitializer->setSessionFactory(sessionFactory);

    wsInitializer->setConfig(config);
    wsInitializer->initWsService(wsService);

    if (!wsService->registerMsgHandler(999,
            [](std::shared_ptr<boostssl::MessageFace> _msg, std::shared_ptr<WsSession> _session) {
                _msg->setRespPacket();
                auto seq = _msg->seq();
                auto data = std::string(_msg->payload()->begin(), _msg->payload()->end());
                // BCOS_LOG(INFO) << LOG_BADGE(" [Main] ===>>>> ") << LOG_DESC(" receive
                // requst seq ")
                //                << LOG_KV("seq", seq);
                // BCOS_LOG(INFO) << LOG_BADGE(" [Main] ===>>>> ") << LOG_DESC(" receive
                // requst message ")
                //                << LOG_KV("data", data);
                BCOS_LOG(INFO) << LOG_BADGE(" [Main] ===>>>> ") << LOG_DESC(" receive requst msg")
                               << LOG_KV("version", _msg->version()) << LOG_KV("seq", _msg->seq())
                               << LOG_KV("packetType", _msg->packetType())
                               << LOG_KV("ext", _msg->ext()) << LOG_KV("data", data);
                _session->asyncSendMessage(_msg);
            }))
    {
        BCOS_LOG(WARNING) << "registerMsgHandler failed";
        return EXIT_SUCCESS;
    }

    auto handler = wsService->getMsgHandler(999);
    if (!handler)
    {
        BCOS_LOG(WARNING) << "msg handler not found";
        return EXIT_SUCCESS;
    }

    wsService->start();

    int i = 0;
    while (true)
    {
        TEST_SERVER_LOG(INFO, MODULE_NAME) << LOG_BADGE(" [Main] ===>>>> ");
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));
        i++;
    }

    return EXIT_SUCCESS;
}