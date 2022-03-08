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
 * @file main.cpp
 * @author: octopus
 * @date 2021-05-25
 */
#include <clocale>
#include <iostream>
#include <thread>

#include "../common/FrontServiceBuilder.h"

using namespace std;
using namespace bcos;
using namespace gateway;

#define GATEWAY_MAIN_LOG(LEVEL) BCOS_LOG(LEVEL) << "[Gateway][MAIN]"

int main(int argc, const char** argv)
{
    if ((argc == 2) && ((std::string(argv[1]) == "-h") || (std::string(argv[1]) == "--help")))
    {
        std::cerr << "./gateway-exec-mini groupID nodeID ./config.ini" << std::endl;
        return -1;
    }

    if (argc <= 3)
    {
        std::cerr << "please input groupID、nodeID、config path" << std::endl;
        return -1;
    }

    std::string groupID = argv[1];
    std::string nodeID = argv[2];
    std::string configPath = argv[3];

    try
    {
        // create frontService
        auto frontService = buildFrontService(groupID, nodeID, configPath);
        auto fsWeakptr = std::weak_ptr<bcos::front::FrontService>(frontService);
        // register message dispatcher for front service
        frontService->registerModuleMessageDispatcher(
            bcos::protocol::ModuleID::AMOP, [fsWeakptr](bcos::crypto::NodeIDPtr _nodeID,
                                                const std::string& _id, bytesConstRef _data) {
                auto frontService = fsWeakptr.lock();
                if (frontService)
                {
                    GATEWAY_MAIN_LOG(INFO)
                        << LOG_DESC("echo") << LOG_KV("to", _nodeID->hex())
                        << LOG_KV("content", std::string(_data.begin(), _data.end()));
                    frontService->asyncSendResponse(
                        _id, bcos::protocol::ModuleID::AMOP, _nodeID, _data, [](Error::Ptr) {});
                }
            });

        while (true)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            frontService->asyncGetNodeIDs(
                [frontService](Error::Ptr _error, std::shared_ptr<const crypto::NodeIDs> _nodeIDs) {
                    (void)_error;
                    if (!_nodeIDs || _nodeIDs->empty())
                    {
                        return;
                    }

                    for (const auto& nodeID : *_nodeIDs)
                    {
                        std::string randStr =
                            boost::uuids::to_string(boost::uuids::random_generator()());
                        GATEWAY_MAIN_LOG(INFO) << LOG_DESC("request") << LOG_KV("to", nodeID->hex())
                                               << LOG_KV("content", randStr);

                        auto payload = bytesConstRef((bcos::byte*)randStr.data(), randStr.size());

                        frontService->asyncSendMessageByNodeID(bcos::protocol::ModuleID::AMOP,
                            nodeID, payload, 0,
                            [randStr](Error::Ptr _error, bcos::crypto::NodeIDPtr _nodeID,
                                bytesConstRef _data, const std::string& _id,
                                bcos::front::ResponseFunc _respFunc) {
                                (void)_respFunc;
                                if (_error && (_error->errorCode() != 0))
                                {
                                    GATEWAY_MAIN_LOG(ERROR)
                                        << LOG_DESC("request error") << LOG_KV("to", _nodeID->hex())
                                        << LOG_KV("id", _id);
                                    return;
                                }

                                std::string retMsg = std::string(_data.begin(), _data.end());
                                if (retMsg == randStr)
                                {
                                    GATEWAY_MAIN_LOG(INFO)
                                        << LOG_DESC("response ok") << LOG_KV("from", _nodeID->hex())
                                        << LOG_KV("id", _id);
                                }
                                else
                                {
                                    GATEWAY_MAIN_LOG(ERROR)
                                        << LOG_DESC("response error")
                                        << LOG_KV("from", _nodeID->hex()) << LOG_KV("id", _id)
                                        << LOG_KV("req", randStr) << LOG_KV("rep", retMsg);
                                }
                            });
                    }
                });
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