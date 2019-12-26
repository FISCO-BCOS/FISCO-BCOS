/*
    This file is part of cpp-ethereum.

    cpp-ethereum is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    cpp-ethereum is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <libdevcore/Common.h>
#include <libdevcore/TrieCommon.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace std;
using namespace dev;
using namespace dev::test;

BOOST_AUTO_TEST_SUITE(Crypto)

BOOST_FIXTURE_TEST_SUITE(TrieCommon, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(NibbleSlice)
{
    bytesConstRef b1("ffff1");
    bytesConstRef b2("ffff");
    ::NibbleSlice nb1(b1, 0);
    ::NibbleSlice nb2(b2, 0);

    LOG(INFO) << "NibbleSlice1: " << nb1;
    LOG(INFO) << "NibbleSlice2: " << nb2;

    BOOST_CHECK(nb2.isEarlierThan(nb1));
    nb1.clear();
    LOG(INFO) << "Clear NibbleSlice1";
    LOG(INFO) << "NibbleSlice1: " << nb1;
    LOG(INFO) << "NibbleSlice2: " << nb2;
    BOOST_CHECK(nb1.empty());
    BOOST_CHECK(nb1 != nb2);
    BOOST_CHECK(!nb2.isEarlierThan(nb1));
}

BOOST_AUTO_TEST_SUITE_END() BOOST_AUTO_TEST_SUITE_END()
