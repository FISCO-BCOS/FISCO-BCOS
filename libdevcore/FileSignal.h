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
 * (c) 2016-2019 fisco-dev contributors.
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

namespace dev
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
}  // namespace dev
