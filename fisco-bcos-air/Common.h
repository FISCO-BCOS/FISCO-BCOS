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
 * @file Common.h
 * @author: yujiechen
 * @date 2021-06-11
 */

#pragma once
#include "libinitializer/Common.h"
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <atomic>
#include <chrono>
#include <ctime>
#include <iostream>
#include <memory>

namespace bcos
{
namespace node
{
class ExitHandler
{
public:
    void exit() { exitHandler(0); }
    static void exitHandler(int) { ExitHandler::c_shouldExit.store(true); }
    bool shouldExit() const { return ExitHandler::c_shouldExit.load(); }

    static std::atomic_bool c_shouldExit;
};
std::atomic_bool ExitHandler::c_shouldExit = {false};

void setDefaultOrCLocale()
{
#if __unix__
    if (!std::setlocale(LC_ALL, ""))
    {
        setenv("LC_ALL", "C", 1);
    }
#endif
}

struct Params
{
    std::string configFilePath;
    std::string genesisFilePath;
    float txSpeed;
};

Params initAirNodeCommandLine(int argc, const char* argv[], bool _autoSendTx)
{
    boost::program_options::options_description main_options("Usage of FISCO-BCOS");
    main_options.add_options()("help,h", "print help information")(
        "version,v", "version of FISCO-BCOS")("config,c",
        boost::program_options::value<std::string>()->default_value("./config.ini"),
        "config file path, eg. config.ini")("genesis,g",
        boost::program_options::value<std::string>()->default_value("./config.genesis"),
        "genesis config file path, eg. genesis.ini");

    if (_autoSendTx)
    {
        main_options.add_options()(
            "txSpeed,t", boost::program_options::value<float>(), "transaction generate speed");
    }
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
        bcos::initializer::printVersion();
        exit(0);
    }
    std::string configPath("./config.ini");
    if (vm.count("config"))
    {
        configPath = vm["config"].as<std::string>();
    }
    if (vm.count("c"))
    {
        configPath = vm["c"].as<std::string>();
    }
    std::string genesisFilePath("./config.genesis");
    if (vm.count("genesis"))
    {
        genesisFilePath = vm["genesis"].as<std::string>();
    }
    if (vm.count("g"))
    {
        genesisFilePath = vm["g"].as<std::string>();
    }
    if (!boost::filesystem::exists(configPath))
    {
        std::cout << "config \'" << configPath << "\' not found!";
        exit(0);
    }
    if (!boost::filesystem::exists(genesisFilePath))
    {
        std::cout << "genesis config \'" << genesisFilePath << "\' not found!";
        exit(0);
    }
    float txSpeed = 10;
    if (_autoSendTx)
    {
        if (vm.count("txSpeed") || vm.count("t"))
        {
            txSpeed = vm["txSpeed"].as<float>();
        }
    }
    return Params{configPath, genesisFilePath, txSpeed};
}
}  // namespace node
}  // namespace bcos