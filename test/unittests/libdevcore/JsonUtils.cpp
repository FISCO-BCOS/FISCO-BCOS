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
 * @author chaychen
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
    const std::string fieldName1 = "int";
    const std::string fieldName2 = "string";
    const std::string fieldName3 = "bool";

    json_spirit::mObject obj1 = json_spirit::mObject();
    obj1[fieldName1] = 42;
    obj1[fieldName2] = "East Street";

    json_spirit::mObject obj2 = obj1;
    obj2[fieldName3] = true;

    std::set<std::string> allowedFields;
    allowedFields.insert(fieldName1);
    allowedFields.insert(fieldName2);

    // The expected result is success.
    validateFieldNames(obj1, allowedFields);
    // The expected result is an exception thrown.
    BOOST_CHECK_THROW(validateFieldNames(obj2, allowedFields), UnknownField);

    // The expected result is success.
    requireJsonFields(obj1, "config.address",
        {{fieldName1, {{json_spirit::int_type}, JsonFieldPresence::Required}},
            {fieldName2, {{json_spirit::str_type}, JsonFieldPresence::Required}}});
    requireJsonFields(obj1, "config.address",
        {{fieldName1, {{json_spirit::int_type}, JsonFieldPresence::Required}},
            {fieldName2, {{json_spirit::str_type}, JsonFieldPresence::Required}},
            {fieldName3, {{json_spirit::bool_type}, JsonFieldPresence::Optional}}});
    // The expected result is an exception thrown.
    BOOST_CHECK_THROW(
        requireJsonFields(obj2, "config.address",
            {{fieldName1, {{json_spirit::int_type}, JsonFieldPresence::Required}},
                {fieldName2, {{json_spirit::str_type}, JsonFieldPresence::Required}}}),
        UnknownField);
    BOOST_CHECK_THROW(
        requireJsonFields(obj1, "config.address",
            {{fieldName1, {{json_spirit::int_type}, JsonFieldPresence::Required}},
                {fieldName2, {{json_spirit::str_type}, JsonFieldPresence::Required}},
                {fieldName3, {{json_spirit::bool_type}, JsonFieldPresence::Required}}}),
        MissingField);
    BOOST_CHECK_THROW(
        requireJsonFields(obj1, "config.address",
            {{fieldName1, {{json_spirit::str_type}, JsonFieldPresence::Required}},
                {fieldName2, {{json_spirit::str_type}, JsonFieldPresence::Required}}}),
        WrongFieldType);

    BOOST_CHECK(jsonTypeAsString(obj2.at(fieldName1).type()) == "json Int");
    BOOST_CHECK(jsonTypeAsString(obj2.at(fieldName2).type()) == "json String");
    BOOST_CHECK(jsonTypeAsString(obj2.at(fieldName3).type()) == "json Bool");
    json_spirit::Value_type type = json_spirit::array_type;
    BOOST_CHECK(jsonTypeAsString(type) == "json Array");
    type = json_spirit::real_type;
    BOOST_CHECK(jsonTypeAsString(type) == "json Real");
    type = json_spirit::null_type;
    BOOST_CHECK(jsonTypeAsString(type) == "json Null");
    type = json_spirit::obj_type;
    BOOST_CHECK(jsonTypeAsString(type) == "json Object");
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace dev
