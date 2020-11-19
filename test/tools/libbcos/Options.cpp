/*
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
 */
/**
 * @brief Class for handling test-fisco-bcos custom options
 *
 * @file Options.cpp
 * @author yujiechen
 * @date 2018-08-28
 */

#include "Options.h"
#include <boost/program_options.hpp>
#include <boost/test/unit_test.hpp>
using namespace boost::unit_test;

using namespace std;
using namespace boost;
using namespace bcos::test;
using namespace bcos::eth;
namespace bcos
{
namespace test
{
Options::Options(int argc, char** argv)
{
    namespace po = boost::program_options;

    po::options_description test_options("test of FISCO-BCOS:");

    test_options.add_options()("testpath,t", boost::program_options::value<std::string>(),
        " set test path")("help,h", "help of FISCO-BCOS");

    po::variables_map vm;
    try
    {
        po::store(boost::program_options::parse_command_line(argc, argv, test_options), vm);
    }
    catch (...)
    {
        std::cout << "invalid input" << std::endl;
    }
    if (vm.count("help") || vm.count("h"))
    {
        std::cout << test_options << std::endl;
        exit(0);
    }
    else if (vm.count("--all"))
        all = true;
    else if (vm.count("testpath") || vm.count("t"))
        testpath = vm["testpath"].as<std::string>();
    /// set VM related options
    setVmOptions(argc, argv);
}

/// set vm related options (for test libexecutive/VMFactory.cpp)
void Options::setVmOptions(int argc, char** argv)
{
    namespace po = boost::program_options;
    po::options_description vm_options = vmProgramOptions();
    po::variables_map vm;
    try
    {
        po::store(boost::program_options::parse_command_line(argc, argv, vm_options), vm);
    }
    catch (...)
    {
        std::cout << "invalid input in setVmOptions" << std::endl;
    }
    /// get evm name
    if (vm.count("vm"))
    {
        vm_name = vm["vm"].as<std::string>();
        std::cout << "vm:" << vm["vm"].as<std::string>() << "\n";
    }
    std::vector<std::string> evmc_option_vec;
    /// get evmc options
    if (vm.count("evmc "))
    {
        evmc_option_vec = vm["evmc "].as<std::vector<std::string>>();
        std::cout << "evmc_option_vec size:" << evmc_option_vec.size() << "\n";
    }
}

Options const& Options::get(int argc, char** argv)
{
    static Options instance(argc, argv);
    return instance;
}

Options const& Options::get()
{
    return get(framework::master_test_suite().argc, framework::master_test_suite().argv);
}
}  // namespace test
}  // namespace bcos
