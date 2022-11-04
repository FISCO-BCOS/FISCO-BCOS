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
 * @file Address.h
 * @author: jimmyshi
 * @date: 2022-11-04
 */

#pragma once
#include <string>

namespace bcos
{
class AddressUtils
{
public:
    static std::string padding(const std::string& addr)
    {
        if (addr.length() > 40)
        {
            return addr.substr(addr.length() - 40, 40);
        }
        else
        {
            // start with 0000
            return std::string(40 - addr.length(), '0') + addr;
        }
    }
};


}  // namespace bcos