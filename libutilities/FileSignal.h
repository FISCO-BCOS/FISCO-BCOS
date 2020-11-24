/**
 *  Copyright (C) 2020 FISCO BCOS.
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
 * @brief: Use file to trigger handler
 *
 * @file: FileSignal.h
 * @author: jimmyshi
 * @date 2019-07-08
 */
#pragma once
#include <boost/filesystem.hpp>
#include <cstdio>
#include <functional>
#include <iostream>
#include <memory>
#include <string>

namespace bcos
{
class FileSignal
{
public:
    static void callIfFileExist(const std::string& _file, std::function<void(void)> _f)
    {
        try
        {
            if (boost::filesystem::exists(_file))
            {
                boost::filesystem::remove(_file);  // Just call once, even _f() exception happens
                _f();
                if (boost::filesystem::exists(_file))
                {
                    // Delete file signal generated during f() is executing
                    boost::filesystem::remove(_file);
                }
            }
        }
        catch (std::exception& _e)
        {
            if (boost::filesystem::exists(_file))
            {
                // Delete file signal generated during f() is executing
                boost::filesystem::remove(_file);
            }
            std::cerr << "FISCO BCOS file signal error: "
                      << "file: " << _file << " what: " << _e.what() << std::endl;
        }
    }
};
}  // namespace bcos
