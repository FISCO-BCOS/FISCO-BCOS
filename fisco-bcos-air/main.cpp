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
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-tool/NodeConfig.h>
#include <bcos-utilities/BoostLogInitializer.h>
#include <execinfo.h>

using namespace bcos::node;
using namespace bcos::initializer;
using namespace bcos::node;

int main(int argc, const char* argv[])
{
    /// set LC_ALL
    setDefaultOrCLocale();
    // FIB-184: the log-sink abort path is now closed at the source — every async sink in
    // BoostLogInitializer installs make_exception_suppressor(), so an exception on the log
    // feeding thread drops the record instead of reaching std::terminate. This handler is kept
    // only as a last-resort backstop for genuinely unexpected terminations; it must NOT swallow
    // exceptions (that would hide unrelated crashes), it only prints a backtrace then aborts.
    std::set_terminate([]() {
        std::cerr << "terminate handler called, print stacks" << std::endl;
        void* trace_elems[50];
        int trace_elem_count(backtrace(trace_elems, 50));
        char** stack_syms(backtrace_symbols(trace_elems, trace_elem_count));
        for (int i = 0; i < trace_elem_count; ++i)
        {
            std::cout << stack_syms[i] << "\n";
        }
        free(stack_syms);
        std::cerr << "terminate handler called, print stack end" << std::endl;
        abort();
    });

    // Note: the initializer must exist in the life time of the whole program
    auto initializer = std::make_shared<AirNodeInitializer>();
    try
    {
        auto param = bcos::initializer::initAirNodeCommandLine(argc, argv, false);
        if (param.op != bcos::initializer::Params::operation::None)
        {
            if (param.hasOp(bcos::initializer::Params::operation::Prune))
            {
                initializer->init(param.configFilePath, param.genesisFilePath);
                std::cout << "[" << bcos::getCurrentDateTime() << "] ";
                std::cout << "prune the node data..." << std::endl;
                initializer->nodeInitializer()->prune();
                std::cout << "[" << bcos::getCurrentDateTime() << "] ";
                std::cout << "prune the node data success." << std::endl;
                return 0;
            }
            auto logInitializer = std::make_shared<bcos::BoostLogInitializer>();
            logInitializer->initLog(param.configFilePath);
            auto nodeConfig = std::make_shared<bcos::tool::NodeConfig>(
                std::make_shared<bcos::crypto::KeyFactoryImpl>());
            nodeConfig->loadGenesisConfig(param.genesisFilePath);
            nodeConfig->loadConfig(param.configFilePath);
            auto nodeInitializer = std::make_shared<bcos::initializer::Initializer>();
            nodeInitializer->initConfig(param.configFilePath, param.genesisFilePath, "", true);
            if (param.hasOp(bcos::initializer::Params::operation::Snapshot) ||
                param.hasOp(bcos::initializer::Params::operation::SnapshotWithoutTxAndReceipt))
            {
                bool withTxAndReceipt = param.hasOp(bcos::initializer::Params::operation::Snapshot);
                std::cout << "[" << bcos::getCurrentDateTime() << "] ";
                std::cout << "generating snapshot to " << param.snapshotPath << " ..." << std::endl;
                auto error = nodeInitializer->generateSnapshot(
                    param.snapshotPath, withTxAndReceipt, nodeConfig);
                if (error)
                {
                    std::cout << "[" << bcos::getCurrentDateTime() << "] ";
                    std::cout << "generate snapshot failed, error:" << error->errorMessage()
                              << std::endl;
                    return -1;
                }
                std::cout << "[" << bcos::getCurrentDateTime() << "] ";
                std::cout << "generate snapshot success." << std::endl;
                return 0;
            }
            if (param.hasOp(bcos::initializer::Params::operation::ImportSnapshot))
            {
                // initializer->init(param.configFilePath, param.genesisFilePath);
                std::cout << "[" << bcos::getCurrentDateTime() << "] ";
                std::cout << "importing snapshot from " << param.snapshotPath << " ..."
                          << std::endl;
                auto error = nodeInitializer->importSnapshot(param.snapshotPath, nodeConfig);
                if (error)
                {
                    std::cout << "[" << bcos::getCurrentDateTime() << "] ";
                    std::cout << "import snapshot failed, error:" << error->errorMessage()
                              << std::endl;
                    return -1;
                }
                std::cout << "[" << bcos::getCurrentDateTime() << "] ";
                std::cout << "import snapshot success." << std::endl;
            }
            return 0;
        }
        initializer->init(param);
        bcos::initializer::showNodeVersionMetric();

        bcos::initializer::printVersion();
        std::cout << "[" << bcos::getCurrentDateTime() << "] ";
        std::cout << "The fisco-bcos is running..." << std::endl;

        // Register signal handlers BEFORE start() so that crashes during
        // start() are properly logged instead of killing the process silently.
        ExitHandler::registerSignalHandlers();

        initializer->start();

        // Re-register signal handlers AFTER start() because sub-components
        // (e.g., the TARS RPC framework via tars::Application::main()) may
        // have installed their own handlers in the meantime.
        //
        // registerSignalHandlers() uses sigaction() to save the previously
        // installed handler.  When a signal arrives, ExitHandler::exitHandler()
        // sets c_shouldExit (unblocking main()) and then chain-calls the
        // saved handler so that TARS can also perform its graceful shutdown.
        ExitHandler::registerSignalHandlers();

        // If a signal was already received during start() (before the
        // re-registration above), c_shouldExit is already true and the
        // wait(false) below returns immediately.
    }
    catch (std::exception const& e)
    {
        bcos::initializer::printVersion();
        std::cout << "[" << bcos::getCurrentDateTime() << "] ";
        std::cout << "start fisco-bcos failed, error:" << boost::diagnostic_information(e)
                  << std::endl;
        return -1;
    }

    ExitHandler::c_shouldExit.wait(false);

    initializer.reset();
    std::cout << "[" << bcos::getCurrentDateTime() << "] ";
    std::cout << "fisco-bcos program exit normally." << std::endl;
}