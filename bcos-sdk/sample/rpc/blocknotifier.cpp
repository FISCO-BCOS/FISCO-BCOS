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
 * @file blocknotifier.cpp
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
#include <cstddef>
#include <cstdlib>
#include <fstream>
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
    std::cerr << "Desc: print block notifier by command params\n";
    std::cerr << "Usage: blocknotifier <config> <group> \n"
              << "Example:\n"
              << "    ./blocknotifier ./config_sample.ini group "
                 "\n";
    std::exit(0);
}


int main(int argc, char** argv)
{
    if (argc < 3)
    {
        usage();
    }

    std::string config = argv[1];
    std::string group = argv[2];

    std::cout << LOG_DESC(" [BlockNotifier] params ===>>>> ") << LOG_KV("\n\t # config", config)
              << LOG_KV("\n\t # group", group) << std::endl;

    auto factory = std::make_shared<SdkFactory>();
    // construct cpp-sdk object
    auto sdk = factory->buildSdk(config);
    // start sdk
    sdk->start();

    std::cout << LOG_DESC(" [BlockNotifier] start sdk ... ") << std::endl;

    sdk->service()->registerBlockNumberNotifier(
        group, [](const std::string& _group, int64_t _blockNumber) {
            std::cout << " \t block notifier ===>>>> " << LOG_KV("group", _group)
                      << LOG_KV("blockNumber", _blockNumber) << std::endl;
        });

    int i = 0;
    while (true)
    {
        std::cout << LOG_DESC(" Main thread running ") << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(10000));
        i++;
    }

    return EXIT_SUCCESS;
}