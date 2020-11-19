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
 * @brief: parser of evm
 *
 * @file: EvmParams.h
 * @author: yujiechen
 * @date 2018-10-29
 */
#pragma once

#include <libdevcrypto/Common.h>
#include <libethcore/CommonJS.h>
#include <libutilities/Common.h>
#include <libutilities/CommonIO.h>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <string>
#define EVMC_LOG(DES) LOG(DES) << "[EVM_DEMO]"

struct Input
{
    std::string inputCall;
    bcos::bytes codeData;
    bcos::Address addr;
};
class EvmParams
{
public:
    EvmParams(boost::property_tree::ptree const& pt)
    {
        m_transValue = pt.get<bcos::u256>("evm.transValue", bcos::u256(0));
        m_gas = pt.get<bcos::u256>("evm.gas", bcos::u256(50000000000));
        m_gasPrice = pt.get<bcos::u256>("evm.gasPrice", bcos::u256(0));
        m_gasLimit = pt.get<bcos::u256>("evm.gasLimit", bcos::u256(100000000000));
        m_blockNumber = pt.get<int64_t>("evm.blockNumber", 0);
        ParseCodes(pt);
        ParseInput(pt);
    }
    /// parse params for deploy
    void ParseCodes(boost::property_tree::ptree const& pt)
    {
        try
        {
            for (auto it : pt.get_child("deploy"))
            {
                if (it.first.find("code.") == 0)
                    m_code.push_back(bcos::fromHex(it.second.data()));
            }
        }
        catch (std::exception& e)
        {
            EVMC_LOG(WARNING) << "[EvmParams/deploy section has not been set]";
            return;
        }
    }

    /// parse params for input
    void ParseInput(boost::property_tree::ptree const& pt)
    {
        try
        {
            for (auto it : pt.get_child("call"))
            {
                std::vector<std::string> s;
                try
                {
                    boost::split(s, it.first, boost::is_any_of("."), boost::token_compress_on);
                    if (s.size() != 2)
                    {
                        EVMC_LOG(WARNING) << "[EvmParams/Invalid Key] " << it.first;
                        continue;
                    }
                    std::string call_key = "call." + s[1];
                    int index = boost::lexical_cast<size_t>(s[1]) - 1;
                    if (it.first.find(call_key) == 0)
                    {
                        if (m_input.size() >= boost::lexical_cast<size_t>(s[1]))
                            m_input[index].inputCall = it.second.data();
                        else
                        {
                            Input input;
                            input.inputCall = it.second.data();
                            m_input.push_back(input);
                        }
                    }
                    std::string address_key = "addr." + s[1];
                    if (it.first.find(address_key) == 0)
                    {
                        if (m_input.size() < boost::lexical_cast<size_t>(s[1]))
                        {
                            Input input;
                            input.addr = bcos::Address(it.second.data());
                            m_input.push_back(input);
                        }
                        else
                            m_input[index].addr = bcos::Address(it.second.data());
                        continue;
                    }
                    std::string code_key = "code." + s[1];
                    if (it.first.find(code_key) == 0)
                    {
                        if (m_input.size() < boost::lexical_cast<size_t>(s[1]))
                        {
                            Input input;
                            input.codeData = bcos::fromHex(it.second.data());
                            m_input.push_back(input);
                        }
                        else
                            m_input[index].codeData = bcos::fromHex(it.second.data());
                    }
                }
                catch (std::exception& e)
                {
                    EVMC_LOG(WARNING)
                        << "[EvmParams/Parse Key Failed] " << boost::diagnostic_information(e);
                    continue;
                }
            }
        }
        catch (std::exception& e)
        {
            EVMC_LOG(WARNING) << "[EvmParams/call section]  has not been set, "
                              << boost::diagnostic_information(e);
        }
    }

    /// get interfaces
    bcos::u256 const& transValue() const { return m_transValue; }
    bcos::u256 const& gas() const { return m_gas; }
    bcos::u256 const& gasLimit() const { return m_gasLimit; }
    bcos::u256 const& gasPrice() const { return m_gasPrice; }
    std::vector<bcos::bytes> const& code() { return m_code; }
    int64_t const& blockNumber() const { return m_blockNumber; }
    std::vector<bcos::bytes> const& code() const { return m_code; }
    std::vector<Input>& input() { return m_input; }

private:
    bcos::u256 m_transValue;
    bcos::u256 m_gas;
    bcos::u256 m_gasLimit;
    bcos::u256 m_gasPrice;
    /// transaction code
    std::vector<bcos::bytes> m_code;
    std::vector<Input> m_input;
    int64_t m_blockNumber;
};
