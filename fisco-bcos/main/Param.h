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
 * @file: ParamParse.h
 * @author: chaychen
 * @date 2018-10-09
 */

#pragma once
#include <include/BuildInfo.h>
#include <libdevcore/Common.h>
#include <libethcore/Common.h>
#include <boost/program_options.hpp>
#include <iostream>
#include <memory>

namespace dev
{
char const* Version = FISCO_BCOS_PROJECT_VERSION;
}

class MainParams
{
public:
    MainParams(boost::program_options::variables_map const& vm) : m_vmMap(vm) { loadConfigs(); }
    std::string const& configPath() const { return m_configPath; }

private:
    void loadConfigs() { setConfigPath(); }
    void setConfigPath()
    {
        if (m_vmMap.count("config") || m_vmMap.count("c"))
            m_configPath = m_vmMap["config"].as<std::string>();
    }

private:
    std::string m_configPath = "config.ini";
    boost::program_options::variables_map m_vmMap;
};

void version()
{
    std::cout << "FISCO-BCOS Version : " << dev::Version << std::endl;
    std::cout << "Build Time         : " << DEV_QUOTED(FISCO_BCOS_BUILD_TIME) << std::endl;
    std::cout << "Build Type         : " << DEV_QUOTED(FISCO_BCOS_BUILD_PLATFORM) << "/"
              << DEV_QUOTED(FISCO_BCOS_BUILD_TYPE) << std::endl;
    std::cout << "Git Branch         : " << DEV_QUOTED(FISCO_BCOS_BUILD_BRANCH) << std::endl;
    std::cout << "Git Commit Hash    : " << DEV_QUOTED(FISCO_BCOS_COMMIT_HASH) << std::endl;
}

MainParams initCommandLine(int argc, const char* argv[])
{
    boost::program_options::options_description main_options("Main for FISCO-BCOS");
    main_options.add_options()("help,h", "help of FISCO-BCOS")(
        "version,v", "version of FISCO-BCOS")(
        "config,c", boost::program_options::value<std::string>(), "configuration path");
    boost::program_options::variables_map vm;
    try
    {
        boost::program_options::store(
            boost::program_options::parse_command_line(argc, argv, main_options), vm);
    }
    catch (...)
    {
        std::cout << "invalid input" << std::endl;
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
    MainParams m_params(vm);
    return m_params;
}
