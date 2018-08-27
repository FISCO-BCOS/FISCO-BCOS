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
 * @brief
 *
 * @file FileSystem.cpp
 * @author: tabsu
 * @date 2018-08-24
 */

#include <libdevcore/FileSystem.h>
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(FileSystem)

/**
 * @brief Construct a new boost auto test case object,
 * For libdevcore/FileSystem.cpp,setDataDir,getDataDir,
 * setIpcPath, getIpcPath
 * @author: tabsu
 * @date 2018-08-24
 */
BOOST_AUTO_TEST_CASE(testFileSystem)
{
    dev::setDataDir("./data");
    BOOST_CHECK((dev::getDataDir("ethereum").string() == "./data") == true);
    BOOST_CHECK((dev::getDataDir("test").filename() == ".test") == true);
    BOOST_CHECK((dev::getDataDir("").string() == "./data") == true);

    dev::setIpcPath("/xxx/geth.ipc");
    BOOST_CHECK((dev::getIpcPath().string() == "/xxx") == true);

    dev::setIpcPath("/xxx/eth.ipc");
    BOOST_CHECK((dev::getIpcPath().string() == "/xxx/eth.ipc") == true);

    dev::setIpcPath("geth.ipc");
}

BOOST_AUTO_TEST_SUITE_END()
