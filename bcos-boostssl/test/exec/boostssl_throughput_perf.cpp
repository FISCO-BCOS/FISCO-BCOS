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
 * @file boostssl_throughput_perf.cpp
 * @author: octopus
 * @date 2022-04-08
 */

#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsInitializer.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/BoostLogInitializer.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/ThreadPool.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

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
              << " \t boostssl-throughput-perf server <ip> <port> <disable_ssl> <thread_count>\n "
              << " \t boostssl-throughput-perf client <ip> <port> <disable_ssl> <thread_count> "
                 "<msg_size> <send rate>\n"
              << "Example:\n"
              << " \t ./boostssl-throughput-perf server 127.0.0.1 20200 true 16\n"
              << " \t ./boostssl-throughput-perf client 127.0.0.1 20200 true 16 1024 1000\n";
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

void workAsClient(std::string serverIp, uint16_t serverPort, bool disableSsl, uint64_t sendRate,
    uint64_t msgSize, uint32_t threadCount)
{
    std::cerr << " boostssl_throughput_perf work as client." << std::endl
              << " \t serverIp: " << serverIp << std::endl
              << " \t serverPort: " << serverPort << std::endl
              << " \t disableSsl: " << disableSsl << std::endl
              << " \t sendRate: " << sendRate << std::endl
              << " \t msgSize: " << msgSize << std::endl
              << " \t threadCount: " << threadCount << "\n\n\n";

    auto config = std::make_shared<WsConfig>();
    config->setModel(WsModel::Client);

    auto peers = std::make_shared<EndPoints>();
    peers->insert(NodeIPEndpoint(serverIp, serverPort));

    config->setConnectPeers(peers);

    config->setThreadPoolSize(threadCount);
    config->setDisableSsl(disableSsl);
    if (!config->disableSsl())
    {
        auto contextConfig = std::make_shared<ContextConfig>();
        contextConfig->initConfig("./boostssl.ini");
        config->setContextConfig(contextConfig);
    }

    auto wsService = std::make_shared<ws::WsService>("boostssl-th-perf-client");
    auto wsInitializer = std::make_shared<WsInitializer>();

    wsInitializer->setConfig(config);
    wsInitializer->initWsService(wsService);
    wsService->start();

    std::string strMsg(msgSize, 'a');
    auto msg = wsService->messageFactory()->buildMessage();
    msg->setPacketType(DELAY_PERF_MSGTYPE);
    msg->setPayload(std::make_shared<bytes>(strMsg.begin(), strMsg.end()));

    std::atomic<uint64_t> nSucC = 0;
    std::atomic<uint64_t> nFailedC = 0;
    std::atomic<uint64_t> nLastSucC = 0;
    std::atomic<uint64_t> nLastFailedC = 0;

    std::atomic<uint64_t> nThisSendCount = 0;
    std::atomic<uint64_t> nLastSendCount = 0;

    uint64_t sendMsgCountPerMS = sendRate / 1000;
    sendMsgCountPerMS = sendMsgCountPerMS > 0 ? sendMsgCountPerMS : 1;
    // auto startPoint = std::chrono::high_resolution_clock::now();

    // report thread;
    auto reportThread = std::make_shared<std::thread>(
        [&wsService, &nLastSendCount, &nSucC, &nFailedC, &nLastSucC, &nLastFailedC]() {
            uint32_t nSleepMS = 1000;
            while (true)
            {
                int64_t nQueueSize = -1;
                auto ss = wsService->sessions();
                if (!ss.empty())
                {
                    nQueueSize = ss[0]->msgQueueSize();
                }

                std::cerr << " boostssl throughput perf working as client: " << std::endl;
                std::cerr << " \tnQueueSize: " << nQueueSize << ", nSucC: " << nSucC
                          << ", nFailedC: " << nFailedC << ", nLastSucC: " << nLastSucC
                          << ", nLastFailedC: " << nLastFailedC
                          << ", nLastSendCount: " << nLastSendCount << std::endl;

                nLastFailedC = 0;
                nLastSucC = 0;
                nLastSendCount = 0;
                std::this_thread::sleep_for(std::chrono::milliseconds(nSleepMS));
            }
        });
    reportThread->detach();


    while (true)
    {
        nThisSendCount++;
        nLastSendCount++;
        msg->setSeq(wsService->messageFactory()->newSeq());
        wsService->asyncSendMessage(msg, Options(-1),
            [&nFailedC, &nSucC, &nLastFailedC, &nLastSucC](Error::Ptr _error,
                std::shared_ptr<MessageFace> _msg, std::shared_ptr<WsSession> _session) {
                (void)_error;
                (void)_session;
                (void)_msg;
                if (_error && _error->errorCode() != 0)
                {
                    nFailedC++;
                    nLastFailedC++;
                    return;
                }

                nLastSucC++;
                nSucC++;
            });

        if (nThisSendCount > sendMsgCountPerMS)
        {
            nThisSendCount = 0;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void workAsServer(std::string listenIp, uint16_t listenPort, bool disableSsl, uint32_t threadCount)
{
    std::cerr << " boostssl_throughput_perf work as server." << std::endl
              << " \t listenIp: " << listenIp << std::endl
              << " \t listenPort: " << listenPort << std::endl
              << " \t disableSsl: " << disableSsl << std::endl
              << " \t threadCount: " << threadCount << std::endl;

    auto config = std::make_shared<WsConfig>();
    config->setModel(WsModel::Server);

    config->setListenIP(listenIp);
    config->setListenPort(listenPort);
    config->setThreadPoolSize(threadCount);
    config->setDisableSsl(disableSsl);
    if (!config->disableSsl())
    {
        auto contextConfig = std::make_shared<ContextConfig>();
        contextConfig->initConfig("./boostssl.ini");
        config->setContextConfig(contextConfig);
    }

    auto wsService = std::make_shared<ws::WsService>("boostssl-th-perf-server");
    auto wsInitializer = std::make_shared<WsInitializer>();

    wsInitializer->setConfig(config);
    wsInitializer->initWsService(wsService);

    std::atomic<uint64_t> totalRecvDataSize = {0};
    std::atomic<uint64_t> lastRecvDataCount = {0};
    std::atomic<uint64_t> lastSecTotalRecvDataSize = {0};
    wsService->registerMsgHandler(DELAY_PERF_MSGTYPE,
        [&totalRecvDataSize, &lastSecTotalRecvDataSize, &lastRecvDataCount](
            std::shared_ptr<MessageFace> _msg, std::shared_ptr<WsSession> _session) {
            totalRecvDataSize += _msg->payload()->size();
            lastSecTotalRecvDataSize += _msg->payload()->size();
            lastRecvDataCount++;
            _session->asyncSendMessage(_msg);
        });

    wsService->start();

    uint32_t nSleepMS = 1000;
    while (true)
    {
        std::cerr << " boostssl throughput perf working as server: " << std::endl;
        std::cerr << " \t ClientCount: " << wsService->sessions().size()
                  << ", TotalRecvDataSize(Bytes): " << totalRecvDataSize
                  << ", lastRecvDataCount: " << lastRecvDataCount
                  << ", LastRecvDataSize(Bytes): " << lastSecTotalRecvDataSize
                  << ", LastRecvDataRate(MBit/s): "
                  << (((double)lastSecTotalRecvDataSize * 8 * 1000) / nSleepMS / (1024 * 1024))
                  << std::endl;
        lastSecTotalRecvDataSize = 0;
        lastRecvDataCount = 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(nSleepMS));
    }
}

int main(int argc, char** argv)
{
    if (argc < 5)
    {
        usage();
    }

    std::string workModel = argv[1];
    std::string host = argv[2];
    uint16_t port = atoi(argv[3]);
    bool disableSsl = ("true" == std::string(argv[4])) ? true : false;
    uint32_t threadCount = atoi(argv[5]);

    initLog();

    if (workModel == "server")
    {
        workAsServer(host, port, disableSsl, threadCount);
    }
    else if (workModel == "client")
    {
        uint64_t sendRate = 1000;
        uint64_t msgSize = 1024;

        if (argc > 6)
        {
            msgSize = std::stoull(std::string(argv[6]));
        }

        if (argc > 7)
        {
            sendRate = std::stoull(std::string(argv[7]));
        }

        workAsClient(host, port, disableSsl, sendRate, msgSize, threadCount);
    }
    else
    {
        usage();
    }
}