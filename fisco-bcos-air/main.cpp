/**
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
 * @brief main for the fisco-bcos
 * @file main.cpp
 * @author: yujiechen
 * @date 2021-07-26
 * @brief main for the fisco-bcos
 * @file main.cpp
 * @author: ancelmo
 * @date 2021-10-14
 */
#include "AirNodeInitializer.h"
#include "Common.h"
#include "libinitializer/CommandHelper.h"
#include <thread>
#include <stdexcept>
#include <execinfo.h>

using namespace bcos::node;
using namespace bcos::initializer;
using namespace bcos::node;

int main(int argc, const char* argv[])
{
    /// set LC_ALL
    setDefaultOrCLocale();
    std::set_terminate([]() {
        std::cerr << "terminate handler called, print stacks" << std::endl;
        void* trace_elems[20];
        int trace_elem_count(backtrace(trace_elems, 20));
        char** stack_syms(backtrace_symbols(trace_elems, trace_elem_count));
        for (int i = 0; i < trace_elem_count; ++i)
        {
            std::cout << stack_syms[i] << "\n";
        }
        free(stack_syms);
        std::cerr << "terminate handler called, print stack end" << std::endl;
        abort();
    });
    // get datetime and output welcome info
    ExitHandler exitHandler;
    signal(SIGTERM, &ExitHandler::exitHandler);
    signal(SIGABRT, &ExitHandler::exitHandler);
    signal(SIGINT, &ExitHandler::exitHandler);
    // Note: the initializer must exist in the life time of the whole program
    auto initializer = std::make_shared<AirNodeInitializer>();
    try
    {
        auto param = bcos::initializer::initAirNodeCommandLine(argc, argv, false);
        initializer->init(param.configFilePath, param.genesisFilePath);
        bcos::initializer::showNodeVersionMetric();
        initializer->start();
    }
    catch (std::exception const& e)
    {
        bcos::initializer::printVersion();
        std::cout << "[" << bcos::getCurrentDateTime() << "] ";
        std::cout << "start fisco-bcos failed, error:" << boost::diagnostic_information(e)
                  << std::endl;
        return -1;
    }
    bcos::initializer::printVersion();
    std::cout << "[" << bcos::getCurrentDateTime() << "] ";
    std::cout << "The fisco-bcos is running..." << std::endl;
    while (!exitHandler.shouldExit())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    initializer.reset();
    std::cout << "[" << bcos::getCurrentDateTime() << "] ";
    std::cout << "fisco-bcos program exit normally." << std::endl;
}