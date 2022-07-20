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
 * @file broadcast.cpp
 * @author: octopus
 * @date 2021-08-24
 */
#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsMessage.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-boostssl/websocket/WsSession.h>
#include <bcos-cpp-sdk/SdkFactory.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/ThreadPool.h>
#include <boost/core/ignore_unused.hpp>
#include <memory>
#include <set>
#include <string>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::boostssl;
using namespace bcos;
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

void usage()
{
    std::cerr << "Desc: broadcast amop message by command params\n";
    std::cerr << "Usage: broadcast <config> <topic> <message>\n"
              << "Example:\n"
              << "    ./broadcast ./config_sample.ini topic HelloWorld\n";
    std::exit(0);
}


int main(int argc, char** argv)
{
    if (argc < 4)
    {
        usage();
    }

    std::string config = argv[1];
    std::string topic = argv[2];
    std::string msg = argv[3];

    std::cout << LOG_DESC(" [AMOP][Broadcast]] params ===>>>> ") << LOG_KV("\n\t # config", config)
              << LOG_KV("\n\t # topic", topic) << LOG_KV("\n\t # message", msg) << std::endl;

    auto factory = std::make_shared<SdkFactory>();
    // construct cpp-sdk object
    auto sdk = factory->buildSdk(config);
    // start sdk
    sdk->start();

    std::cout << LOG_DESC(" [AMOP][Broadcast] start sdk ... ") << std::endl;

    int i = 0;
    while (true)
    {
        std::cout << LOG_DESC(" broadcast message ===>>>> ") << LOG_KV("topic", topic)
                  << LOG_KV("message", msg) << std::endl;

        sdk->amop()->broadcast(topic, bytesConstRef((byte*)msg.data(), msg.size()));
        std::this_thread::sleep_for(std::chrono::milliseconds(5000));
        i++;
    }

    return EXIT_SUCCESS;
}