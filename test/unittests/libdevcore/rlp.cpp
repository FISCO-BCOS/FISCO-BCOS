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
 * @brief: RLP test functions.
 *
 * @file rlp.cpp
 * @author: yujiechen
 * @date 2018-08-24
 */

#include "rlp.h"
#include <libdevcore/Common.h>
#include <test/tools/libutils/Common.h>

namespace dev
{
namespace test
{
void RlpTest::runRlpTest(std::string _name, fs::path const& _path)
{
    fs::path const testPath = dev::test::getTestPath() / _path;
    try
    {
        LOG(INFO) << "TEST " << _name << ":";
        js::mValue v = js::mValue();
        std::string filePath = (testPath / fs::path(_name + ".json")).string();
        std::string const s = dev::asString(dev::contents(filePath));
        std::string empty_string =
            "Contents of " + (testPath / fs::path(_name + ".json")).string() + " is empty";
        BOOST_REQUIRE_MESSAGE(s.length() > 0, empty_string);
        json_spirit::read_string(s, v);
        doRlpTests(v);
    }
    catch (Exception const& _e)
    {
        BOOST_ERROR("Failed test with Exception: " << diagnostic_information(_e));
    }
    catch (std::exception const& _e)
    {
        BOOST_ERROR("Failed test with Exception: " << _e.what());
    }
}


/*
 * @ function: check string transformed to serialized RLP &&
 *             serialized RLP transformed to RLP structure
 * @ params: json object with "in" to be transformed into rlp or "VALID" "INVALID"
 *           "out" is the valid or invalid  rlp string
 * @ return:
 */
void RlpTest::doRlpTests(js::mValue const& jsonObj)
{
    for (auto& i : jsonObj.get_obj())
    {
        std::string testName;
        LOG(INFO) << " " << i.first;
        testName = "[" + i.first + "]";
        // js::mObject const& obj = i.second.get_obj();
        js::mObject obj = i.second.get_obj();
        BOOST_REQUIRE_MESSAGE(obj.count("out") > 0, testName + " out has not been set !");
        BOOST_REQUIRE_MESSAGE(!obj.at("out").is_null(), testName + " out has been set to null !");
        BOOST_REQUIRE_MESSAGE(obj.count("in") > 0, testName + " in has not been set !");
        RlpType rlpType = RlpType::Test;
        if (obj.at("in").type() == js::str_type)
        {
            if (obj.at("in").get_str() == "INVALID")
                rlpType = RlpType::Invalid;
            else if (obj.at("in").get_str() == "VALID")
                rlpType = RlpType::Valid;
        }
        if (rlpType == RlpType::Test)
        {
            // check whether "in" transformed rlp string is equal to "out"
            checkEncode(testName, obj);
        }
        // check decode
        bool exception = false;
        checkDecode(obj, rlpType, exception);
        // invalid rlp string && exceptioned
        if (rlpType == RlpType::Invalid && exception)
            continue;
        if (rlpType == RlpType::Invalid && !exception)
            BOOST_ERROR(testName + " Expect RLP Exception as rlp is invalid");
        if (exception)  // rlp valid, but trigger exception
            BOOST_ERROR(testName + " Unexpected RLP Exception as rlp is valid");
    }
}

/*
 * @ function: transform json object to serialized RLPstream
 * @ param: (1) jsonObj: json object to be transformed to RLP
 *          (2) retRlp: serialized rlp stream transformed from json object
 * @ return: retRlp: serialized rlp stream
 */
void RlpTest::buildRLP(js::mValue const& jsonObj, dev::RLPStream& retRlp)
{
    // array jason
    if (jsonObj.type() == js::array_type)
    {
        dev::RLPStream item_rlp;
        for (auto& json_item : jsonObj.get_array())
            buildRLP(json_item, item_rlp);
        retRlp.appendList(item_rlp.out());
    }
    else if (jsonObj.type() == js::int_type)
    {
        retRlp.append(jsonObj.get_uint64());
    }
    else if (jsonObj.type() == js::str_type)
    {
        auto str = jsonObj.get_str();
        if (str.size() && str[0] == '#')
            retRlp.append(bigint(str.substr(1)));
        else
            retRlp.append(str);
    }
}

/*
 * @ function: check whether json object transformed rlp is equal to origin json object
 * @ param: (1) jsonObj: json object transformed to rlp
 *          (2) rlp: rlp object transformed from json object
 * @ return:
 */
void RlpTest::checkRlp(js::mValue const& jsonObj, dev::RLP const& rlp)
{
    if (jsonObj.type() == js::array_type)
    {
        // ##check type
        // must be list
        BOOST_CHECK(rlp.isList());
        // must be not int or data
        BOOST_CHECK(!rlp.isInt());
        BOOST_CHECK(!rlp.isData());
        // compare value
        auto const& json_array = jsonObj.get_array();
        for (unsigned i = 0; i < json_array.size(); i++)
            checkRlp(json_array[i], rlp[i]);
    }
    else if (jsonObj.type() == js::int_type)
    {
        BOOST_CHECK(rlp.isInt());
        BOOST_CHECK(!rlp.isList());
        BOOST_CHECK(!rlp.isNull());
        BOOST_CHECK(rlp == jsonObj.get_int());
    }
    else if (jsonObj.type() == js::str_type)
    {
        std::string str = jsonObj.get_str();
        if (!str.empty() && str.front() == '#')
        {
            // Deal with bigint instead of a raw string
            std::string bigIntStr = str.substr(1, str.length() - 1);
            std::stringstream bintStream(bigIntStr);
            bigint val;
            bintStream >> val;
            BOOST_CHECK(!rlp.isList());
            BOOST_CHECK(!rlp.isNull());
            BOOST_CHECK(rlp == val);
        }
        else
        {
            BOOST_CHECK(!rlp.isList());
            BOOST_CHECK(!rlp.isNull());
            BOOST_CHECK(rlp.isData());
            BOOST_CHECK(rlp == jsonObj.get_str());
        }
    }
}
}  // namespace test
}  // namespace dev
