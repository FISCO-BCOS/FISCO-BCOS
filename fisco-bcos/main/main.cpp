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
#include "ExitHandler.h"
#include <include/BuildInfo.h>
#include <libdevcore/easylog.h>
#include <libinitializer/Initializer.h>
#include <boost/program_options.hpp>
#include <clocale>
#include <ctime>
#include <iostream>
#include <memory>

#if FISCO_EASYLOG
INITIALIZE_EASYLOGGINGPP
#endif

using namespace std;
using namespace dev::initializer;

void setDefaultOrCLocale()
{
#if __unix__
    if (!std::setlocale(LC_ALL, ""))
    {
        setenv("LC_ALL", "C", 1);
    }
#endif
}

void version()
{
    std::cout << "FISCO-BCOS Version : " << FISCO_BCOS_PROJECT_VERSION << std::endl;
    std::cout << "Build Time         : " << DEV_QUOTED(FISCO_BCOS_BUILD_TIME) << std::endl;
    std::cout << "Build Type         : " << DEV_QUOTED(FISCO_BCOS_BUILD_PLATFORM) << "/"
              << DEV_QUOTED(FISCO_BCOS_BUILD_TYPE) << std::endl;
    std::cout << "Git Branch         : " << DEV_QUOTED(FISCO_BCOS_BUILD_BRANCH) << std::endl;
    std::cout << "Git Commit Hash    : " << DEV_QUOTED(FISCO_BCOS_COMMIT_HASH) << std::endl;
}

string initCommandLine(int argc, const char* argv[])
{
    boost::program_options::options_description main_options("Usage of FISCO-BCOS");
    main_options.add_options()("help,h", "print help information")(
        "version,v", "version of FISCO-BCOS")("config,c",
        boost::program_options::value<std::string>(), "config file path, eg. config.ini");
    boost::program_options::variables_map vm;
    try
    {
        boost::program_options::store(
            boost::program_options::parse_command_line(argc, argv, main_options), vm);
    }
    catch (...)
    {
        std::cout << "invalid parameters" << std::endl;
        std::cout << main_options << std::endl;
        exit(0);
    }
    /// help information
    if (vm.count("help") || vm.count("h"))
    {
        std::cout << main_options << std::endl;
        exit(0);
    }
    /// version information
    if (vm.count("version") || vm.count("v"))
    {
        version();
        exit(0);
    }
    string configPath("./config.ini");
    if (vm.count("config") || vm.count("c"))
    {
        configPath = vm["config"].as<std::string>();
    }
    else if (boost::filesystem::exists(configPath))
    {
        std::cout << "use default configPath : " << configPath << std::endl;
    }
    else
    {
        std::cout << main_options << std::endl;
        exit(0);
    }

    return configPath;
}

int main(int argc, const char* argv[])
{
    /// set LC_ALL
    setDefaultOrCLocale();
    /// init params
    string configPath = initCommandLine(argc, argv);
    /// callback initializer to init all ledgers
    auto initialize = std::make_shared<Initializer>();
    try
    {
        std::cout << "Initializing..." << std::endl;
        initialize->init(configPath);
    }
    catch (std::exception& e)
    {
        std::cerr << "Init failed!!!" << std::endl;
        return -1;
    }
    version();
    // get datetime and output welcome info
    char buffer[40];
    auto currentTime = chrono::system_clock::to_time_t(chrono::system_clock::now());
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&currentTime));
    std::cout << "[" << buffer << "] ";
    std::cout << "The FISCO-BCOS is running..." << std::endl;
    ExitHandler exitHandler;
    signal(SIGABRT, &ExitHandler::exitHandler);
    signal(SIGTERM, &ExitHandler::exitHandler);
    signal(SIGINT, &ExitHandler::exitHandler);

    while (!exitHandler.shouldExit())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        LogInitializer::logRotateByTime();
    }
    initialize.reset();
    currentTime = chrono::system_clock::to_time_t(chrono::system_clock::now());
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", localtime(&currentTime));
    std::cout << "[" << buffer << "] ";
    std::cout << "FISCO-BCOS program exit normally." << std::endl;
    return 0;
}
