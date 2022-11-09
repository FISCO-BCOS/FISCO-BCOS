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
 * @brief Address utils
 * @file AddressUtils.h
 * @author: jimmyshi
 * @date: 2022-11-04
 */

#pragma once
#include <iomanip>
#include <sstream>
#include <string>

namespace bcos
{
class AddressUtils
{
public:
    static std::string padding(std::string_view addr)
    {
        if (addr.length() > 40)
        {
            // remove starting chars, left 40 chars.
            return addr.substr(addr.length() - 40, 40);
        }
        else
        {
            // add 0 at staring making to 40 chars
            std::stringstream stream;
            stream << std::setfill('0') << std::setw(40) << addr;
            return stream.str();
        }
    }
};


}  // namespace bcos