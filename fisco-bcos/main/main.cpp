/**
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 *
 * @brief: empty framework for main of FISCO-BCOS
 *
 * @file: main.cpp
 * @author: yujiechen
 * @date 2018-08-24
 */
#include "Common.h"
#include <include/BuildInfo.h>
#include <libdevcore/Common.h>
#include <libdevcore/FileSignal.h>
#include <libinitializer/Initializer.h>
#include <boost/program_options.hpp>
#include <clocale>
#include <iostream>
#include <memory>
#include <thread>

using namespace std;
using namespace dev;
using namespace dev::initializer;

void checkAndCall(const std::string& configPath, shared_ptr<Initializer> initializer)
{
    std::string moreGroupSignal = configPath + ".append_group";
    dev::FileSignal::callIfFileExist(moreGroupSignal, [&]() {
        cout << "Start more group" << endl;
        initializer->ledgerInitializer()->startMoreLedger();
    });

    std::string resetCalSignal = configPath + ".reset_certificate_whitelist";
    dev::FileSignal::callIfFileExist(resetCalSignal, [&]() {
        cout << "Reset certificate whitelist(CAL)" << endl;
        initializer->p2pInitializer()->resetWhitelist(configPath);
    });
}

int main(int argc, const char* argv[])
{
    /// set LC_ALL
    setDefaultOrCLocale();
    std::set_terminate([]() {
        std::cerr << "terminate handler called" << endl;
        abort();
    });
    /// init params
    string configPath = initCommandLine(argc, argv);
    // get datetime and output welcome info
    ExitHandler exitHandler;
    signal(SIGTERM, &ExitHandler::exitHandler);
    signal(SIGABRT, &ExitHandler::exitHandler);
    signal(SIGINT, &ExitHandler::exitHandler);
    /// callback initializer to init all ledgers
    auto initialize = std::make_shared<Initializer>();
    try
    {
        std::cout << "[" << getCurrentDateTime() << "] ";
        std::cout << "Initializing..." << std::endl;
        initialize->init(configPath);
    }
    catch (std::exception& e)
    {
        std::cerr << "Init failed!!!" << std::endl;
        return -1;
    }
    dev::version();
    std::cout << "[" << getCurrentDateTime() << "] ";
    std::cout << "The FISCO-BCOS is running..." << std::endl;

    while (!exitHandler.shouldExit())
    {
        checkAndCall(configPath, initialize);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    initialize.reset();
    std::cout << "[" << getCurrentDateTime() << "] ";
    std::cout << "FISCO-BCOS program exit normally." << std::endl;

    return 0;
}
