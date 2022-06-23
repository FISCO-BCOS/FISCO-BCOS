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


#define TEST_LOG(LEVEL, module_name) BCOS_LOG(LEVEL) << LOG_BADGE(module_name) << "[WS][SERVICE]"

void usage()
{
    std::cerr << "Usage: echo-client-sample <peerIp> <peerPort> <ssl> <dataSize>\n"
              << "Example:\n"
              << "    ./echo-client-sample 127.0.0.1 20200 true 2\n"
              << "    ./echo-client-sample 127.0.0.1 20200 false 2\n";
    std::exit(0);
}

int main(int argc, char** argv)
{
    if (argc < 5)
    {
        usage();
    }

    std::string host = argv[1];
    uint16_t port = atoi(argv[2]);

    std::string disableSsl = "true";
    uint16_t sizeNum = 1;
    // uint16_t interval = 10;

    if (argc > 3)
    {
        disableSsl = argv[3];
    }

    if (argc > 4)
    {
        sizeNum = atoi(argv[4]);
    }

    // if (argc > 5)
    // {
    //     interval = atoi(argv[5]);
    // }

    std::string test_module_name = "testClient";
    TEST_LOG(INFO, test_module_name)
        << LOG_DESC("echo-client-sample") << LOG_KV("ip", host) << LOG_KV("port", port)
        << LOG_KV("disableSsl", disableSsl) << LOG_KV("datasize", sizeNum);

    auto config = std::make_shared<WsConfig>();
    config->setModel(WsModel::Client);

    NodeIPEndpoint endpoint = NodeIPEndpoint(host, port);

    auto peers = std::make_shared<EndPoints>();
    peers->insert(endpoint);
    config->setConnectPeers(peers);

    config->setThreadPoolSize(1);
    config->setDisableSsl(0 == disableSsl.compare("true"));
    if (!config->disableSsl())
    {
        auto contextConfig = std::make_shared<ContextConfig>();
        contextConfig->initConfig("./boostssl.ini");
        config->setContextConfig(contextConfig);
    }
    config->setModuleName("TEST_CLIENT");

    auto wsService = std::make_shared<ws::WsService>(config->moduleName());
    auto wsInitializer = std::make_shared<WsInitializer>();

    auto sessionFactory = std::make_shared<WsSessionFactory>();
    wsInitializer->setSessionFactory(sessionFactory);

    wsInitializer->setConfig(config);
    wsInitializer->initWsService(wsService);

    wsService->start();

    // construct message
    auto msg = std::dynamic_pointer_cast<WsMessage>(wsService->messageFactory()->buildMessage());
    msg->setPacketType(999);

    std::string randStr(sizeNum, 'a');

    msg->setPayload(std::make_shared<bytes>(randStr.begin(), randStr.end()));

    TEST_LOG(INFO, test_module_name) << LOG_BADGE(" [Main] ===>>>> ") << LOG_DESC("send request")
                                     << LOG_KV("request size", randStr.size());

    int i = 0;
    while (true)
    {
        auto seq = wsService->messageFactory()->newSeq();
        msg->setSeq(seq);
        TEST_LOG(INFO, "TEST_CLIENT_MODULE") << LOG_BADGE(" [msg] ===>>>> ") << LOG_KV("seq", seq)
                                             << LOG_KV("msg_seq()", msg->seq());
        wsService->asyncSendMessage(msg, Options(-1),
            [msg](Error ::Ptr _error, std::shared_ptr<boostssl::MessageFace>,
                std::shared_ptr<WsSession> _session) {
                (void)_session;
                BCOS_LOG(INFO) << LOG_BADGE(" [send message] ===>>>> ")
                               << LOG_DESC(" send requst msg") << LOG_KV("version", msg->version())
                               << LOG_KV("seq", msg->seq())
                               << LOG_KV("packetType", msg->packetType())
                               << LOG_KV("ext", msg->ext())
                               << LOG_KV("data",
                                      std::string(msg->payload()->begin(), msg->payload()->end()));
                if (_error && _error->errorCode() != 0)
                {
                    TEST_LOG(WARNING, "TEST_CLIENT_MODULE")
                        << LOG_BADGE(" [Main] ===>>>> ") << LOG_DESC("callback response error")
                        << LOG_KV("errorCode", _error->errorCode())
                        << LOG_KV("errorMessage", _error->errorMessage());
                    return;
                }

                // auto resp = std::string(_msg->payload()->begin(),
                // _msg->payload()->end()); BCOS_LOG(INFO) << LOG_BADGE(" [Main]
                // ===>>>> ") << LOG_DESC("receive response")
                //               << LOG_KV("resp", resp);
            });

        // if (i % interval == 0)
        // {
        //     std::this_thread::sleep_for(std::chrono::milliseconds(1));
        // }

        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        i++;
    }

    return EXIT_SUCCESS;
}