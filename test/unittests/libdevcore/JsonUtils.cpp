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
/** @file JsonUtils.cpp
 * @author chaychen <c@ethdev.com>
 * @date 2018
 */

#include <json_spirit/JsonSpiritHeaders.h>
#include <libdevcore/Assertions.h>
#include <libdevcore/JsonUtils.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace std;
using namespace dev;
using namespace boost::unit_test;

namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(JsonUtils, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(testJsonUtils)
{
    const std::string fieldName1 = "number";
    const std::string fieldName2 = "road";

    json_spirit::mObject obj = json_spirit::mObject();
    obj[fieldName1] = 42;
    obj[fieldName2] = "East Street";

    std::set<std::string> allowedFields;
    allowedFields.insert(fieldName1);
    allowedFields.insert(fieldName2);

    validateFieldNames(obj, allowedFields);

    requireJsonFields(obj, "config.address",
        {{fieldName1, {{json_spirit::int_type}, JsonFieldPresence::Required}},
            {fieldName2, {{json_spirit::str_type}, JsonFieldPresence::Required}}});

    BOOST_CHECK(jsonTypeAsString(obj.at(fieldName1).type()) == "json Int");
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace dev
