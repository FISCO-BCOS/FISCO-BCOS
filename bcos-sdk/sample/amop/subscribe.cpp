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
 * @file sub_client.cpp
 * @author: octopus
 * @date 2021-08-24
 */
#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsMessage.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-boostssl/websocket/WsSession.h>
#include <bcos-cpp-sdk/SdkFactory.h>
#include <bcos-cpp-sdk/amop/AMOP.h>
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

void usage()
{
    std::cerr << "Desc: subscribe amop topic by command params\n";
    std::cerr << "Usage: subscribe <config> <topic>\n"
              << "Example:\n"
              << "    ./subscribe ./config_sample.ini topic\n";
    std::exit(0);
}

//------------------------------------------------------------------------------
int main(int argc, char** argv)
{
    if (argc < 3)
    {
        usage();
    }

    std::string config = argv[1];
    std::set<std::string> topicList;
    for (int i = 2; i < argc; i++)
    {
        topicList.insert(argv[i]);
    }
    std::string topic = argv[2];

    std::cout << LOG_DESC(" [AMOP][Subscribe] params ===>>>> ") << LOG_KV("\n\t # config", config)
              << LOG_KV("\n\t # topic", topic) << std::endl;

    auto factory = std::make_shared<SdkFactory>();
    // construct cpp-sdk object
    auto sdk = factory->buildSdk(config);
    // start sdk
    sdk->start();

    std::cout << LOG_DESC(" [AMOP][Subscribe] start sdk ... ") << std::endl;
    sdk->amop()->setSubCallback(
        [&sdk](Error::Ptr _error, const std::string& _endPoint, const std::string& _seq,
            bytesConstRef _data, std::shared_ptr<bcos::boostssl::ws::WsSession> _session) {
            boost::ignore_unused(_session);
            if (_error)
            {
                std::cout << " \t something is wrong" << LOG_KV("error", _error->errorCode())
                          << LOG_KV("errorMessage", _error->errorMessage()) << std::endl;
            }
            else
            {
                std::cout << " \t recv message and would echo message to publish ===>>>> "
                          << LOG_KV("endPoint", _endPoint)
                          << LOG_KV("msg", std::string(_data.begin(), _data.end())) << std::endl;

                sdk->amop()->sendResponse(_endPoint, _seq, _data);
            }
        });
    sdk->amop()->subscribe(topicList);

    int i = 0;
    while (true)
    {
        std::cout << LOG_DESC(" Main thread running ") << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(10000));
        i++;
    }

    return EXIT_SUCCESS;
}