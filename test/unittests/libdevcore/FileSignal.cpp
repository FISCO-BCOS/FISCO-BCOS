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
 * @brief Construct a new boost auto test case object for Worker
 *
 * @file FileSignal.cpp
 * @author: jimmyshi
 * @date 2019-07-10
 */

#include <libutilities/FileSignal.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <fstream>

using namespace bcos;
using namespace std;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(FileSignal, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(FileSignal)
{
    bool called = false;
    function<void(void)> caller = [&]() { called = true; };
    string file = ".file_signal_test";

    // check the file is not exist
    BOOST_CHECK(!boost::filesystem::exists(file));

    // check caller is not triggered as the file is not exist
    bcos::FileSignal::callIfFileExist(file, caller);
    BOOST_CHECK(!called);

    // create the file
    ofstream fs(file);
    fs << "";
    fs.close();

    // check the file is exist
    BOOST_CHECK(boost::filesystem::exists(file));

    // check caller is triggered as the file is exist
    bcos::FileSignal::callIfFileExist(file, caller);
    BOOST_CHECK(called);

    // check the file is deleted after trigger
    BOOST_CHECK(!boost::filesystem::exists(file));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
