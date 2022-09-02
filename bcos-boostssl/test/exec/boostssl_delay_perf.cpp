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
 * @file boostssl_delay_perf.cpp
 * @author: octopus
 * @date 2021-10-31
 */

#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsInitializer.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/BoostLogInitializer.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/ThreadPool.h>
#include <chrono>
#include <cstdlib>
#include <iostream>
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
    std::cerr << "Usage: \n"
              << " \t boostssl-delay-perf server <ip> <port> <disable_ssl> \n "
              << " \t boostssl-delay-perf client <ip> <port> <disable_ssl> <echo_count> "
                 "<msg_size> \n"
              << "Example:\n"
              << " \t ./boostssl-delay-perf server 127.0.0.1 20200 true \n"
              << " \t ./boostssl-delay-perf client 127.0.0.1 20200 true 100000 1024 \n";
    std::exit(0);
}

void initLog(const std::string& _configPath = "./clog.ini")
{
    boost::property_tree::ptree pt;
    try
    {
        boost::property_tree::read_ini(_configPath, pt);
    }
    catch (const std::exception& e)
    {
        try
        {
            std::string defaultPath = "conf/clog.ini";
            boost::property_tree::read_ini(defaultPath, pt);
        }
        catch (const std::exception& e)
        {
            std::cerr << "Not found available log config(./clog.ini or ./conf/clog.ini), use "
                         "the default configuration items"
                      << std::endl;
        }
    }

    auto logInitializer = new bcos::BoostLogInitializer();
    logInitializer->initLog(pt, bcos::FileLogger, "cpp_sdk_log");
}

const static int DELAY_PERF_MSGTYPE = 9999;

void workAsClient(
    std::string serverIp, uint16_t serverPort, bool disableSsl, uint64_t echoC, uint64_t msgSize)
{
    std::cerr << " ==> boostssl_delay_perf work as client. \n"
              << " \t serverIp: " << serverIp << "\n"
              << " \t serverPort: " << serverPort << "\n"
              << " \t disableSsl: " << disableSsl << "\n"
              << " \t echoC: " << echoC << "\n"
              << " \t msgSize: " << msgSize << "\n\n\n";

    auto config = std::make_shared<WsConfig>();
    config->setModel(WsModel::Client);

    auto peers = std::make_shared<EndPoints>();
    peers->insert(NodeIPEndpoint(serverIp, serverPort));
    config->setConnectPeers(peers);

    config->setThreadPoolSize(4);
    config->setDisableSsl(disableSsl);
    if (!config->disableSsl())
    {
        auto contextConfig = std::make_shared<ContextConfig>();
        contextConfig->initConfig("./boostssl.ini");
        config->setContextConfig(contextConfig);
    }

    auto wsService = std::make_shared<ws::WsService>("boostssl-delay-perf-client");
    auto wsInitializer = std::make_shared<WsInitializer>();

    wsInitializer->setConfig(config);
    wsInitializer->initWsService(wsService);
    wsService->start();


    std::string strMsg(msgSize, 'a');
    auto msg = wsService->messageFactory()->buildMessage();
    msg->setPacketType(DELAY_PERF_MSGTYPE);
    msg->setPayload(std::make_shared<bytes>(strMsg.begin(), strMsg.end()));
    // msg->setSeq(wsService->messageFactory()->newSeq());

    uint64_t nSucC = 0;
    uint64_t nFailedC = 0;

    uint64_t i = 0;
    uint64_t _10Per = echoC / 10;
    auto startPoint = std::chrono::high_resolution_clock::now();
    while (i++ < echoC)
    {
        std::promise<bool> p;
        auto f = p.get_future();

        if (i % _10Per == 0)
        {
            std::cerr << "\t...process: " << ((double)i / echoC) * 100 << "%" << std::endl;
        }

        msg->setSeq(wsService->messageFactory()->newSeq());
        wsService->asyncSendMessage(msg, Options(-1),
            [&p, &nFailedC, &nSucC](Error::Ptr _error, std::shared_ptr<MessageFace> _msg,
                std::shared_ptr<WsSession> _session) {
                (void)_error;
                (void)_session;
                (void)_msg;
                if (_error && _error->errorCode() != 0)
                {
                    nFailedC++;
                    return;
                }
                p.set_value(true);

                nSucC++;
            });
        f.get();
    }
    auto endPoint = std::chrono::high_resolution_clock::now();

    auto totalTime =
        std::chrono::duration_cast<std::chrono::microseconds>(endPoint - startPoint).count();

    std::cerr << std::endl << std::endl;
    std::cerr << " ==> boostssl_delay_perf result: " << std::endl;
    std::cerr << " \t total time(us): " << totalTime << std::endl;
    std::cerr << " \t average time(us): " << ((double)totalTime / echoC) << std::endl;
    std::cerr << " \t nSucC: " << nSucC << std::endl;
    std::cerr << " \t nFailedC: " << nFailedC << std::endl;
}

void workAsServer(std::string listenIp, uint16_t listenPort, bool disableSsl)
{
    std::cerr << " ==> boostssl_delay_perf work as server." << std::endl
              << " \t listenIp: " << listenIp << std::endl
              << " \t listenPort: " << listenPort << std::endl
              << " \t disableSsl: " << disableSsl << "\n";

    auto config = std::make_shared<WsConfig>();
    config->setModel(WsModel::Server);

    config->setListenIP(listenIp);
    config->setListenPort(listenPort);
    config->setThreadPoolSize(4);
    config->setDisableSsl(disableSsl);
    if (!config->disableSsl())
    {
        auto contextConfig = std::make_shared<ContextConfig>();
        contextConfig->initConfig("./boostssl.ini");
        config->setContextConfig(contextConfig);
    }

    auto wsService = std::make_shared<ws::WsService>("boostssl-delay-perf-server");
    auto wsInitializer = std::make_shared<WsInitializer>();

    wsInitializer->setConfig(config);
    wsInitializer->initWsService(wsService);

    wsService->registerMsgHandler(DELAY_PERF_MSGTYPE,
        [](std::shared_ptr<MessageFace> _msg, std::shared_ptr<WsSession> _session) {
            _session->asyncSendMessage(_msg);
        });

    wsService->start();

    int i = 0;
    while (true)
    {
        // std::cerr << " boostssl deplay perf server working ..." << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(10000));
        i++;
    }
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        usage();
    }

    std::string workModel = argv[1];
    std::string host = argv[2];
    uint16_t port = atoi(argv[3]);
    bool disableSsl = ("true" == std::string(argv[4])) ? true : false;

    initLog();

    if (workModel == "server")
    {
        workAsServer(host, port, disableSsl);
    }
    else if (workModel == "client")
    {
        uint64_t echoCount = 100;
        uint64_t msgSize = 1024;
        if (argc > 5)
        {
            echoCount = std::stoull(std::string(argv[5]));
        }

        if (argc > 6)
        {
            msgSize = std::stoull(std::string(argv[6]));
        }
        workAsClient(host, port, disableSsl, echoCount, msgSize);
    }
    else
    {
        usage();
    }
}