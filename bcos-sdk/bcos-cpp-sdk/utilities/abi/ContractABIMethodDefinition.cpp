/*
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
 * @file ContractABIDefinition.cpp
 * @author: octopus
 * @date 2022-05-19
 */

#include <bcos-cpp-sdk/utilities/abi/ContractABIMethodDefinition.h>
#include <stdexcept>

using namespace bcos;
using namespace bcos::cppsdk;
using namespace bcos::cppsdk::abi;

// reduce the array dimension
bool NamedTypeHelper::removeExtent()
{
    auto size = extentsSize();
    if (size > 0)
    {
        m_extents.resize(size - 1);
        return true;
    }

    return false;
}

//
void NamedTypeHelper::reset(const std::string& _str)
{
    // clear old data
    m_type.clear();
    m_baseType.clear();
    m_extents.clear();

    std::string strType = _str;

    // trim blank character
    boost::trim(strType);
    if (strType.empty())
    {
        BOOST_THROW_EXCEPTION(std::runtime_error("empty string, invalid type in string format"));
    }

    // eg: int[1][2][][3]
    auto firstLeftBracket = strType.find('[');
    // int
    std::string baseType = strType.substr(0, firstLeftBracket);

    //  [10][2][3][]
    std::vector<std::size_t> extents;
    std::string::size_type leftBracket = firstLeftBracket;
    std::string::size_type rightBracket = 0;
    std::string::size_type length = strType.size();

    while (leftBracket < length)
    {
        auto leftBracketBak = leftBracket;
        leftBracket = strType.find('[', leftBracketBak);
        rightBracket = strType.find(']', leftBracketBak);

        if (leftBracket == std::string::npos || rightBracket == std::string::npos ||
            leftBracket >= rightBracket)
        {
            BOOST_THROW_EXCEPTION(
                std::runtime_error("invalid type in string format, value: " + _str));
        }

        std::string digit = strType.substr(leftBracket + 1, rightBracket - leftBracket - 1);
        boost::trim(digit);

        if (digit.empty())
        {
            extents.push_back(0);
        }
        else
        {
            try
            {
                std::size_t size = (std::size_t)std::stoi(digit);
                extents.push_back(size);
            }
            catch (std::exception& _e)
            {
                BOOST_THROW_EXCEPTION(
                    std::runtime_error("invalid type in string format, value: " + _str));
            }
        }

        leftBracket = rightBracket + 1;
    }

    m_type = strType;
    m_baseType = baseType;
    m_extents = extents;
}
