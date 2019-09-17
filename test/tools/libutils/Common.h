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
 * @file: Common.h
 * @author: yujiechen
 * @date: 2018
 * common functions of test submodule
 */
#pragma once

#include <test/tools/libbcos/Options.h>
#include <boost/filesystem.hpp>
#include <cstdlib>
#include <string>


namespace dev
{
namespace test
{
namespace constVar
{
const static std::string TEST_PATH = "FISCO_BCOS_TEST_PATH";
}  // namespace constVar

inline boost::filesystem::path getTestPath()
{
    if (dev::test::Options::get().testpath != "")
    {
        return boost::filesystem::path(dev::test::Options::get().testpath);
    }

    std::string testPath = "";
    // get test path from env
    const char* pTestPath = getenv(dev::test::constVar::TEST_PATH.c_str());
    if (!pTestPath)
    {
        testPath = "../../data";
        LOG(WARNING) << "Test environment " << dev::test::constVar::TEST_PATH
                     << " has not been setted, use default";
    }
    else
    {
        testPath = pTestPath;
    }
    return boost::filesystem::path(testPath);
}

inline std::vector<boost::filesystem::path> getFiles(
    boost::filesystem::path const& dirPath, std::set<std::string> const& extensionLists)
{
    std::vector<boost::filesystem::path> files;
    for (std::string const& extItem : extensionLists)
    {
        using fsIterator = boost::filesystem::directory_iterator;
        for (fsIterator it(dirPath); it != fsIterator(); it++)
        {
            if (boost::filesystem::is_regular_file(it->path()) && it->path().extension() == extItem)
                files.push_back(it->path());
        }
    }
    return files;
}

}  // namespace test
}  // namespace dev
