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
#include "NodeServiceApp.h"
#include "libinitializer/Common.h"
#include <bcos-framework/libutilities/Common.h>
#include <chrono>
#include <ctime>

using namespace bcostars;
using namespace bcos;
using namespace bcos::initializer;
int main(int argc, char* argv[])
{
    try
    {
        bcos::initializer::initCommandLine(argc, argv);
        NodeServiceApp app;
        printVersion();
        std::cout << "[" << getCurrentDateTime() << "] ";
        std::cout << "The fisco-bcos is running..." << std::endl;
        app.main(argc, argv);
        app.waitForShutdown();
        std::cout << "[" << getCurrentDateTime() << "] ";
        std::cout << "fisco-bcos program exit normally." << std::endl;
        return 0;
    }
    catch (std::exception& e)
    {
        cerr << "std::exception:" << e.what() << std::endl;
    }
    catch (...)
    {
        cerr << "unknown exception." << std::endl;
    }
    return -1;
}