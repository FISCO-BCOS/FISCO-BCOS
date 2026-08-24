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
    std::cerr << "Usage: msg_codec_test payload_length [--fresh]\n"
              << "Example:\n"
              << "    ./msg_codec_test 1024\n"
              << "    ./msg_codec_test 1024 --fresh   # new buffer per encode\n";
    std::exit(0);
}


int main(int argc, char** argv)
{
    if (argc < 2)
    {
        usage();
    }

    uint16_t payloadLength = atoi(argv[1]);
    // fresh-buffer mode matches the production send path (WsSession::asyncSendMessage
    // allocates a brand-new buffer per send); the default warm-buffer mode reuses one
    // buffer so steady-state hits retained capacity on both old and new encode.
    bool freshBuffer = (argc > 2 && std::string(argv[2]) == "--fresh");

    BCOS_LOG(INFO) << LOG_DESC("Msg Codec Test") << LOG_KV("payload length", payloadLength)
                   << LOG_KV("fresh buffer", freshBuffer);

    std::string str(payloadLength, 'a');
    // construct message
    WsMessage msg;
    msg.setPayload(bytes(str.begin(), str.end()));

    // reference encode: every measured iteration must produce byte-identical output,
    // so the benchmark doubles as a golden smoke check against silent regressions
    bcos::bytes reference;
    if (!msg.encode(reference))
    {
        BCOS_LOG(ERROR) << LOG_DESC("Msg Codec Test") << LOG_DESC("reference encode failed");
        return EXIT_FAILURE;
    }

    // golden check: encode once into a fresh buffer and compare against the
    // reference before timing, so the per-iteration byte comparison does not
    // add O(payload) work inside the measured loop
    {
        bcos::bytes check;
        if (!msg.encode(check) || check != reference)
        {
            BCOS_LOG(ERROR) << LOG_DESC("Msg Codec Test")
                            << LOG_DESC("encode output mismatch with reference");
            return EXIT_FAILURE;
        }
    }

    auto startPoint = std::chrono::high_resolution_clock::now();
    auto lastReport = std::chrono::high_resolution_clock::now();
    int64_t lastEncodeC = 0;
    auto buffer = std::make_shared<bcos::bytes>();
    while (true)
    {
        if (freshBuffer)
        {
            buffer = std::make_shared<bcos::bytes>();
        }
        msg.encode(*buffer);
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