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
 * @file p2p_perf_server.cpp
 * @author: octopus
 * @date 2023-02-17
 */
#include "bcos-gateway/libnetwork/Common.h"
#include "bcos-gateway/libnetwork/Message.h"
#include "bcos-gateway/libp2p/P2PMessage.h"
#include "bcos-utilities/ratelimiter/TimeWindowRateLimiter.h"
#include "bcos-tars-protocol/protocol/ProtocolInfoCodecImpl.h"
#include "bcos-utilities/BoostLogInitializer.h"
#include "bcos-utilities/Common.h"
#include "bcos-utilities/CompositeBuffer.h"
#include <bcos-gateway/GatewayFactory.h>
#include <bcos-utilities/RateCollector.h>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

using namespace std;
using namespace bcos;
using namespace gateway;

#define P2P_PERF_SERVER_LOG(LEVEL) BCOS_LOG(LEVEL) << "[Perf][Server]"

int main(int argc, const char** argv)
{
    if ((argc >= 2) && ((std::string(argv[1]) == "-h") || (std::string(argv[1]) == "--help")))
    {
        std::cerr << "./p2p-echo-perf -h/--help" << std::endl;
        std::cerr << "  server: ./p2p-echo-perf server ./config.ini" << std::endl;
        std::cerr << "  client: ./p2p-echo-perf client ./config.ini MsgSize QPS" << std::endl;
        return -1;
    }

    if (argc < 3)
    {
        std::cerr << "./p2p-echo-perf -h/--help" << std::endl;
        std::cerr << "  server: ./p2p-echo-perf server ./config.ini" << std::endl;
        std::cerr << "  client: ./p2p-echo-perf client ./config.ini MsgSize QPS" << std::endl;
        return -1;
    }

    std::string model = argv[1];
    std::string configPath = argv[2];

    // work as server or client
    bool asServer = model.starts_with("server");
    std::string workModel = (asServer ? "Server" : "Client");
    std::cerr << " ==> Working As" << workModel << std::endl;
    constexpr static uint16_t packageType = 111;
    try
    {
        g_BCOSConfig.setCodec(std::make_shared<bcostars::protocol::ProtocolInfoCodecImpl>());
        auto logInitializer = std::make_shared<BoostLogInitializer>();
        logInitializer->initLog(configPath);

        constexpr static int RATE_REPORT_INTERVAL = 10000;  // 10s
        auto reporter = std::make_shared<RateCollector>(workModel, RATE_REPORT_INTERVAL);
        reporter->start();

        // load the config items
        auto config = std::make_shared<GatewayConfig>();
        config->initConfig(configPath, false);
        config->loadP2pConnectedNodes();

        // disable rip protocol
        config->setEnableRIPProtocol(false);

        // construct P2P service
        auto gatewayFactory = std::make_shared<GatewayFactory>("", "", nullptr);
        auto service = gatewayFactory->buildService(config);
        service->start();

        if (asServer)  // server working
        {
            // register message handler for p2p echo message type
            service->registerHandlerByMsgType(
                packageType, [reporter](NetworkException _exception,
                                 std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _message) {
                    if (_exception.errorCode() != 0)
                    {
                        return;
                    }

                    // std::cerr << "\t[Server] recv request from client: "
                    //           << std::string(_message->payload()->begin(),
                    //           _message->payload()->end())
                    //           << std::endl;
                    // update rate stat
                    reporter->update(_message->length(), true);
                    _message->setRespPacket();
                    // echo message
                    _session->session()->asyncSendMessage(_message);
                });

            while (true)
            {
                auto p2pInfos = service->sessionInfos();
                std::cerr << "\t[Server] connected clients count: " << p2pInfos.size() << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        }
        else  // client working
        {
            std::size_t msgSize = 1024;
            uint64_t QPS = 5000;

            if (argc > 4)
            {
                msgSize = stoul(argv[3]);
                QPS = stoul(argv[4]);
            }

            std::cerr << "\t[Client] msg size:  " << msgSize << " ,QPS: " << QPS << std::endl;

            bcos::ratelimiter::TimeWindowRateLimiter::Ptr timeWindowRateLimiter =
                std::make_shared<bcos::ratelimiter::TimeWindowRateLimiter>(QPS, 1000, false);

            P2PInfos p2pInfos;
            do
            {
                p2pInfos = service->sessionInfos();
                if (!p2pInfos.empty())
                {
                    break;
                }

                std::cerr << "\t[Client] waiting to connect to server ... " << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(10));
            } while (true);

            string p2pID = p2pInfos[0].p2pID;
            string endpoint = p2pInfos[0].nodeIPEndpoint.address();

            std::cerr << "\t[Client] choose client [" << endpoint << ":" << p2pID << "]"
                      << std::endl;

            std::string content = std::string(msgSize, 'a');

            auto messageFactory = service->messageFactory();
            auto message = dynamic_pointer_cast<P2PMessage>(messageFactory->buildMessage());
            auto payload = std::make_shared<bcos::bytes>();
            payload->insert(payload->end(), content.begin(), content.end());
            message->setPayload(payload);

            std::cerr << "\t[Client] payload: " << message->payload()->size() << std::endl;

            message->setPacketType(packageType);
            while (true)
            {
                message->setSeq(messageFactory->newSeq());
                timeWindowRateLimiter->acquire(1);
                reporter->update(message->length(), true);
                service->asyncSendMessageByNodeID(p2pID, message,
                    [reporter](NetworkException _e, std::shared_ptr<P2PSession> _session,
                        std::shared_ptr<P2PMessage> _message) {
                        if (_e.errorCode() != P2PExceptionType::Success)
                        {
                            std::cerr << "\t[Client] recv exception, error code: " << _e.errorCode()
                                      << " ,error message: " << _e.what() << std::endl;
                            return;
                        }

                        // std::cerr << "\t[Client] recv response from server: "
                        //           << std::string(
                        //                  _message->payload()->begin(),
                        //                  _message->payload()->end())
                        //           << std::endl;
                        // auto readPayload = _message->readPayload();
                        // auto refBuffer = readPayload->asRefBuffer();

                        // std::cerr << "\t[Client] recv response from server: "
                        //           << std::string(refBuffer.begin(), refBuffer.end()) <<
                        //           std::endl;
                    });
                // std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "exception throw, error: " << boost::diagnostic_information(e) << std::endl;
        return -1;
    }

    std::cout << "gateway program exit normally." << std::endl;
    return 0;
}