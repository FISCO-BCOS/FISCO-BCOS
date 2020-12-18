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

#include <libp2p/Service.h>
#include <libprotocol/CommonJS.h>
#include <libprotocol/Protocol.h>
#include <libutilities/FixedBytes.h>
#include <boost/program_options.hpp>
#include <cstdlib>
#include <memory>


using namespace bcos;
using namespace bcos::p2p;
using namespace bcos::protocol;
class Params
{
public:
    Params()
      : m_blockSpeed(1), m_txSpeed(10), m_syncSpeed(1), m_totalTransactions(1 << 30), m_groupSize(1)
    {}

    Params(boost::program_options::variables_map const& vm,
        boost::program_options::options_description const& option)
      : m_txSpeed(10), m_groupSize(1)
    {
        initParams(vm, option);
    }

    void initParams(boost::program_options::variables_map const& vm,
        boost::program_options::options_description const&)
    {
        if (vm.count("blockSpeed") || vm.count("b"))
            m_blockSpeed = vm["blockSpeed"].as<float>();
        if (vm.count("txSpeed") || vm.count("t"))
            m_txSpeed = vm["txSpeed"].as<float>();
        if (vm.count("syncSpeed") || vm.count("s"))
            m_syncSpeed = vm["syncSpeed"].as<float>();
        if (vm.count("groupSize") || vm.count("n"))
            m_groupSize = vm["groupSize"].as<int>();
        if (vm.count("totalTx") || vm.count("m"))
            m_totalTransactions = vm["totalTx"].as<int>();
    }

    int groupSize() const { return m_groupSize; }
    float blockSpeed() { return m_blockSpeed; }
    float txSpeed() { return m_txSpeed; }
    float syncSpeed() { return m_syncSpeed; }
    int totalTransactions() { return m_totalTransactions; }


private:
    float m_blockSpeed;
    float m_txSpeed;
    float m_syncSpeed;
    int m_totalTransactions;
    bcos::GROUP_ID m_groupSize;
};

static Params initCommandLine(int argc, const char* argv[])
{
    boost::program_options::options_description server_options("p2p module of FISCO-BCOS");
    server_options.add_options()("txSpeed,t", boost::program_options::value<float>(),
        "transaction generate speed")("blockSpeed,b", boost::program_options::value<float>(),
        "block generate speed")("syncSpeed,s", boost::program_options::value<float>(),
        "sync speed")("totalTx,m", boost::program_options::value<int>(),
        "total transaction number")("help,h", "help of p2p module of FISCO-BCOS");

    boost::program_options::variables_map vm;
    try
    {
        boost::program_options::store(
            boost::program_options::parse_command_line(argc, argv, server_options), vm);
    }
    catch (...)
    {
        std::cout << "invalid input" << std::endl;
        exit(0);
    }
    /// help information
    if (vm.count("help") || vm.count("h"))
    {
        std::cout << server_options << std::endl;
        exit(0);
    }
    Params m_params(vm, server_options);
    return m_params;
}
