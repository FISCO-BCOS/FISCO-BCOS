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
 * @author: yujiechen
 * @date 2022-06-10
 */
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/interfaces/protocol/GlobalConfig.h>
#include <bcos-gateway/GatewayFactory.h>
#include <bcos-tars-protocol/protocol/ProtocolInfoCodecImpl.h>
#include <bcos-tool/NodeConfig.h>
#include <bcos-utilities/BoostLogInitializer.h>
#include <bcos-utilities/RateLimiter.h>
#include <bcos-utilities/ThreadPool.h>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::tool;

void usage()
{
    std::cerr << "Usage: ./echo-client-sample qps(MBit/s) ${server_address} ${port} "
                 "payloadSize(KBytes, default is 1MBytes)\n"
              << std::endl;
    exit(0);
}

void sendMessage(NodeIPEndpoint const& _endPoint, std::shared_ptr<P2PMessage> _msg,
    std::shared_ptr<Service> _service, std::shared_ptr<RateLimiter> _rateLimiter)
{
    while (true)
    {
        _rateLimiter->acquire(1, true);
        auto seq = _service->messageFactory()->newSeq();
        _msg->setSeq(seq);
        auto startT = utcTime();
        auto msgSize = _msg->payload()->size();
        _service->asyncSendMessageByEndPoint(_endPoint, _msg,
            [msgSize, startT](NetworkException _e, std::shared_ptr<P2PSession> _session,
                std::shared_ptr<P2PMessage>) {
                if (_e.errorCode())
                {
                    BCOS_LOG(WARNING) << LOG_DESC("asyncSendMessage network error")
                                      << LOG_KV("code", _e.errorCode()) << LOG_KV("msg", _e.what());
                    return;
                }
                BCOS_LOG(INFO) << LOG_DESC("receiveResponse, timecost:") << (utcTime() - startT)
                               << LOG_KV("msgSize", msgSize);
            });
    }
}

int main(int argc, char** argv)
{
    if (argc <= 3)
    {
        usage();
    }
    std::string hostIp = argv[1];
    uint16_t port = atoi(argv[2]);
    NodeIPEndpoint serverEndPoint(hostIp, port);
    uint64_t qps = (atol(argv[3])) * 1024 * 1024;
    // default payLoadSize is 1MB
    uint64_t payLoadSize = 1024 * 1024;
    if (argc > 3)
    {
        payLoadSize = (atol(argv[4]) * 1024);
    }
    std::cout << "### payLoad:" << payLoadSize << std::endl;
    std::cout << "### qps: " << qps << std::endl;
    int64_t packetQPS = (qps) / (payLoadSize * 8);
    std::cout << "### packetQPS: " << packetQPS << std::endl;

    g_BCOSConfig.setCodec(std::make_shared<bcostars::protocol::ProtocolInfoCodecImpl>());
    auto keyFactory = std::make_shared<bcos::crypto::KeyFactoryImpl>();
    auto nodeConfig = std::make_shared<NodeConfig>();
    std::string configFilePath = "config.ini";
    nodeConfig->loadConfig(configFilePath);

    auto logInitializer = std::make_shared<BoostLogInitializer>();
    boost::property_tree::ptree pt;
    boost::property_tree::read_ini(configFilePath, pt);
    logInitializer->initLog(pt);

    GatewayFactory gatewayFactory(nodeConfig->chainId(), "localClient", nullptr);
    auto gateway = gatewayFactory.buildGateway(configFilePath, true, nullptr, "localClient");
    auto service = std::dynamic_pointer_cast<Service>(gateway->p2pInterface());

    gateway->start();
    // construct message
    auto msg = std::dynamic_pointer_cast<P2PMessage>(service->messageFactory()->buildMessage());
    msg->setPacketType(999);
    std::string randStr(payLoadSize, 'a');
    msg->setPayload(std::make_shared<bytes>(randStr.begin(), randStr.end()));
    auto rateLimiter = std::make_shared<RateLimiter>(packetQPS);
    sendMessage(serverEndPoint, msg, service, rateLimiter);
    return EXIT_SUCCESS;
}