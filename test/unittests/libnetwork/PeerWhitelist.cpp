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
/** @file PeerWhitelist.cpp
 * @author jimmyshi
 * @date 2019
 */

#include <libnetwork/PeerWhitelist.h>
#include <test/tools/libutils/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <iostream>
#include <string>
#include <vector>

using namespace bcos;
using namespace std;
using namespace bcos::test;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(PeerWhitelistTest, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(NodeIDOkTest)
{
    string okIDStr =
        "2e18411c287bc0f6c106fb025210e67dedd90fa30c95b7b21004f5928b8c86aa0c4eb5a769bb5858d5ea69dd4e"
        "4329106e78c501866dcfb829dceb3100000000";
    string nullStr = "";
    string shortStr = "1";
    string illegalTextStr =
        "2e18411c287bc0f6c106fb025210e67dedd90fa30c95b7b21004f5928b8c86aa0c4eb5a769bb5858d5ea69dd4e"
        "4329106e78c501866dcfb829dceb31gggggggg";

    BOOST_CHECK(isNodeIDOk(okIDStr));
    BOOST_CHECK(!isNodeIDOk(nullStr));
    BOOST_CHECK(!isNodeIDOk(shortStr));
    BOOST_CHECK(!isNodeIDOk(illegalTextStr));
}

BOOST_AUTO_TEST_CASE(AllTest)
{
    string id0 =
        "2e18411c287bc0f6c106fb025210e67dedd90fa30c95b7b21004f5928b8c86aa0c4eb5a769bb5858d5ea69dd4e"
        "4329106e78c501866dcfb829dceb3100000000";
    string id1 =
        "2e18411c287bc0f6c106fb025210e67dedd90fa30c95b7b21004f5928b8c86aa0c4eb5a769bb5858d5ea69dd4e"
        "4329106e78c501866dcfb829dceb3111111111";
    string id2 =
        "2e18411c287bc0f6c106fb025210e67dedd90fa30c95b7b21004f5928b8c86aa0c4eb5a769bb5858d5ea69dd4e"
        "4329106e78c501866dcfb829dceb3122222222";
    string id3 =
        "2e18411c287bc0f6c106fb025210e67dedd90fa30c95b7b21004f5928b8c86aa0c4eb5a769bb5858d5ea69dd4e"
        "4329106e78c501866dcfb829dceb3133333333";

    vector<string> strList{id0, id1, id2};


    // construct and set enable
    PeerWhitelist whitelist(strList, true);

    LOG(INFO) << LOG_BADGE("Whitelist") << whitelist.dump();

    BOOST_CHECK(whitelist.enable());

    BOOST_CHECK(whitelist.has(id0));
    BOOST_CHECK(whitelist.has(NodeID(id0)));

    BOOST_CHECK(whitelist.has(id1));
    BOOST_CHECK(whitelist.has(NodeID(id1)));

    BOOST_CHECK(whitelist.has(id2));
    BOOST_CHECK(whitelist.has(NodeID(id2)));

    BOOST_CHECK(!whitelist.has(id3));
    BOOST_CHECK(!whitelist.has(NodeID(id3)));

    // set disable
    whitelist.setEnable(false);

    BOOST_CHECK(!whitelist.enable());

    BOOST_CHECK(whitelist.has(id0));
    BOOST_CHECK(whitelist.has(NodeID(id0)));

    BOOST_CHECK(whitelist.has(id1));
    BOOST_CHECK(whitelist.has(NodeID(id1)));

    BOOST_CHECK(whitelist.has(id2));
    BOOST_CHECK(whitelist.has(NodeID(id2)));

    BOOST_CHECK(whitelist.has(id3));
    BOOST_CHECK(whitelist.has(NodeID(id3)));

    // set enable
    whitelist.setEnable(true);

    BOOST_CHECK(whitelist.enable());

    BOOST_CHECK(whitelist.has(id0));
    BOOST_CHECK(whitelist.has(NodeID(id0)));

    BOOST_CHECK(whitelist.has(id1));
    BOOST_CHECK(whitelist.has(NodeID(id1)));

    BOOST_CHECK(whitelist.has(id2));
    BOOST_CHECK(whitelist.has(NodeID(id2)));

    BOOST_CHECK(!whitelist.has(id3));
    BOOST_CHECK(!whitelist.has(NodeID(id3)));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace bcos
