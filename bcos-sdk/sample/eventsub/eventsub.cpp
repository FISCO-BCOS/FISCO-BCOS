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
 * @file eventsub.cpp
 * @author: octopus
 * @date 2021-08-24
 */

#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsMessage.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-boostssl/websocket/WsSession.h>
#include <bcos-cpp-sdk/SdkFactory.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/ThreadPool.h>
#include <boost/core/ignore_unused.hpp>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <set>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::boostssl;
using namespace bcos;
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

void usage()
{
    std::cerr << "Desc: subscribe contract events by command params\n";
    std::cerr << "Usage: eventsub <config> <group> <from> <to> <address>[Optional]\n";

    std::cerr << "Example:\n"
              << "    ./eventsub ./config_sample.ini group -1 -1"
                 "\n";
    std::exit(0);
}

int main(int argc, char** argv)
{
    if (argc < 5)
    {
        usage();
    }

    // option params
    std::string config = argv[1];
    std::string group = argv[2];
    int64_t from = atoi(argv[3]);
    int64_t to = atoi(argv[4]);

    std::string address;
    if (argc > 5)
    {
        address = argv[5];
    }

    std::cout << LOG_DESC(" [EventSub] params ===>>>> ") << LOG_KV("\n\t # config", config)
              << LOG_KV("\n\t # group", group) << LOG_KV("\n\t # from", from)
              << LOG_KV("\n\t # to", to) << LOG_KV("\n\t # address", address) << std::endl;

    auto factory = std::make_shared<SdkFactory>();
    // construct cpp-sdk object
    auto sdk = factory->buildSdk(config);
    // start sdk
    sdk->start();

    std::cout << LOG_DESC(" [EventSub] start sdk ... ") << std::endl;

    // construct eventsub params
    auto params = std::make_shared<bcos::cppsdk::event::EventSubParams>();
    params->setFromBlock(from);
    params->setToBlock(to);
    if (!address.empty())
    {
        params->addAddress(address);
    }

    sdk->service()->registerBlockNumberNotifier(group, [](const std::string& _group,
                                                           int64_t _blockNumber) {
        // recv block number from server
        std::cout << " \t recv block number notifier from server ===>>>> "
                  << LOG_KV("group", _group) << LOG_KV("blockNumber", _blockNumber) << std::endl;
    });

    // subscribe event
    sdk->eventSub()->subscribeEvent(
        group, params, [](Error::Ptr _error, const std::string& _events) {
            if (_error)
            {
                // error and exit
                std::cout << " \t something is wrong" << LOG_KV("error", _error->errorCode())
                          << LOG_KV("errorMessage", _error->errorMessage()) << std::endl;
                std::exit(-1);
            }
            else
            {
                // recv events from server
                std::cout << " \t recv events from server ===>>>> " << LOG_KV("events", _events)
                          << std::endl;
            }
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