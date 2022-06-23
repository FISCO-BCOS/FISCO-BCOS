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
 * @file msg_codec_perf.cpp
 * @author: octopus
 * @date 2022-03-24
 */

#include "bcos-boostssl/websocket/WsInitializer.h"
#include <bcos-boostssl/websocket/Common.h>
#include <bcos-boostssl/websocket/WsService.h>
#include <bcos-utilities/BoostLog.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/ThreadPool.h>
#include <memory>
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
    std::cerr << "Usage: msg_codec_test payload_length\n"
              << "Example:\n"
              << "    ./msg_codec_test 1024\n";
    std::exit(0);
}


int main(int argc, char** argv)
{
    if (argc < 2)
    {
        usage();
    }

    uint16_t payloadLength = atoi(argv[1]);

    BCOS_LOG(INFO) << LOG_DESC("Msg Codec Test") << LOG_KV("payload length", payloadLength);

    std::string str(payloadLength, 'a');
    auto messageFactory = std::make_shared<WsMessageFactory>();
    // construct message
    auto msg = std::dynamic_pointer_cast<WsMessage>(messageFactory->buildMessage());

    msg->setPayload(std::make_shared<bytes>(str.begin(), str.end()));

    auto startPoint = std::chrono::high_resolution_clock::now();
    auto lastReport = std::chrono::high_resolution_clock::now();
    int64_t lastEncodeC = 0;
    auto buffer = std::make_shared<bcos::bytes>();
    while (true)
    {
        msg->encode(*buffer);
        lastEncodeC++;

        auto now = std::chrono::high_resolution_clock::now();
        auto lastReportMS =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - lastReport).count();
        auto totalReportMS =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - startPoint).count();

        if (lastReportMS >= 1000)
        {
            BCOS_LOG(INFO) << LOG_BADGE(" [Main] ===>>>> ") << LOG_KV("interval(ms)", lastReportMS)
                           << LOG_KV("payload", payloadLength)
                           << LOG_KV("encodeCount", lastEncodeC);
            lastEncodeC = 0;
            lastReport = std::chrono::high_resolution_clock::now();
        }

        if (totalReportMS >= 10000)
        {
            break;
        }
    }

    return EXIT_SUCCESS;
}