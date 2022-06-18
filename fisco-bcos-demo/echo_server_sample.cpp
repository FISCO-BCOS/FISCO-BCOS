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
 * @file echo_server_sample.cpp
 * @author: yujiechen
 * @date 2022-06-10
 */
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/interfaces/protocol/GlobalConfig.h>
#include <bcos-gateway/GatewayFactory.h>
#include <bcos-tars-protocol/protocol/ProtocolInfoCodecImpl.h>
#include <bcos-tool/NodeConfig.h>
#include <bcos-utilities/BoostLogInitializer.h>
#include <bcos-utilities/ThreadPool.h>

using namespace bcos;
using namespace bcos::gateway;
using namespace bcos::tool;

void usage()
{
    std::cerr << "Usage: echo-server-sample <listenIP> <listenPort> <ssl>\n"
              << "Example:\n"
              << "./echo-server-sample\n";
    std::exit(0);
}

int main(int argc, char** argv)
{
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


    service->registerHandlerByMsgType(
        999, [](NetworkException _e, std::shared_ptr<P2PSession> _session, P2PMessage::Ptr _msg) {
            if (_e.errorCode())
            {
                return;
            }
            auto startT = utcTime();
            _msg->setRespPacket();
            _session->session()->asyncSendMessage(_msg);
            BCOS_LOG(INFO) << LOG_DESC("sendResponse") << LOG_KV("timeCost", (utcTime() - startT))
                           << LOG_KV("msgSize", (_msg->payload()->size()));
        });
    while (true)
    {
        // TEST_SERVER_LOG(INFO, MODULE_NAME) << LOG_BADGE(" [Main] ===>>>> ");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return EXIT_SUCCESS;
}