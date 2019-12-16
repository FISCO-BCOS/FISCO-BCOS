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
 * @brief: class to handle exit
 *
 * @file: ExitHandler.h
 * @author: yujiechen
 * @date 2018-11-07
 */
#pragma once
#include "libconfig/GlobalConfigure.h"
#include "libinitializer/GlobalConfigureInitializer.h"
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <ctime>

class ExitHandler
{
public:
    void exit() { exitHandler(0); }
    static void exitHandler(int) { dev::g_BCOSConfig.shouldExit.store(true); }
    bool shouldExit() const { return dev::g_BCOSConfig.shouldExit.load(); }
};

void setDefaultOrCLocale()
{
#if __unix__
    if (!std::setlocale(LC_ALL, ""))
    {
        setenv("LC_ALL", "C", 1);
    }
#endif
}

std::string initCommandLine(int argc, const char* argv[])
{
    boost::program_options::options_description main_options("Usage of FISCO-BCOS");
    main_options.add_options()("help,h", "print help information")(
        "version,v", "version of FISCO-BCOS")("config,c",
        boost::program_options::value<std::string>()->default_value("./config.ini"),
        "config file path, eg. config.ini")(
        "uncheckroot", "uncheck the stateRoot/receiptsRoot after verify transactions");
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
        dev::version();
        exit(0);
    }
    if (vm.count("uncheckroot"))
    {
        dev::g_BCOSConfig.setUnCheckRoot(true);
    }
    std::string configPath("./config.ini");
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
