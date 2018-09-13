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
 * @brief Construct a new boost auto test case object for FileSystem
 *
 * @file FileSystem.cpp
 * @author: tabsu
 * @date 2018-08-24
 */

#include <libdevcore/FileSystem.h>
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(FileSystem)

BOOST_AUTO_TEST_CASE(testFileSystem)
{
    dev::setDataDir("./data");
    /// test getDataDir
    BOOST_CHECK((dev::getDataDir("ethereum").string() == "./ethereum") == true);
    BOOST_CHECK((dev::getDataDir("fisco-bcos-data").string() == "./data") == true);
    BOOST_CHECK((dev::getDataDir("test").filename() == "test") == true);
    BOOST_CHECK((dev::getDataDir().string() == "./data") == true);
    BOOST_CHECK(dev::getDataDir("/data").string() == "/data");

    /// test getLedgerDir
    BOOST_CHECK(dev::getLedgerDir("ledger1").string() == "./data/ledger1");
    BOOST_CHECK(dev::getLedgerDir("ledger1", "/data").string() == "/data/ledger1");

    dev::setIpcPath("/xxx/geth.ipc");
    BOOST_CHECK((dev::getIpcPath().string() == "/xxx") == true);

    dev::setIpcPath("/xxx/eth.ipc");
    BOOST_CHECK((dev::getIpcPath().string() == "/xxx/eth.ipc") == true);

    dev::setIpcPath("geth.ipc");
}

BOOST_AUTO_TEST_SUITE_END()
