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
#include <libutilities/Common.h>
#include <test/tools/libutils/Common.h>

namespace bcos
{
namespace test
{
void RlpTest::runRlpTest(std::string _name, fs::path const& _path)
{
    fs::path const testPath = bcos::test::getTestPath() / _path;
    try
    {
        LOG(INFO) << "TEST " << _name << ":";
        std::string filePath = (testPath / fs::path(_name + ".json")).string();
        std::string const s = bcos::asString(bcos::contents(filePath));
        std::string empty_string =
            "Contents of " + (testPath / fs::path(_name + ".json")).string() + " is empty";
        BOOST_REQUIRE_MESSAGE(s.length() > 0, empty_string);

        Json::Value v;
        Json::Reader reader;
        reader.parse(s, v);
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
void RlpTest::doRlpTests(Json::Value const& jsonObj)
{
    Json::Value::Members mem = jsonObj.getMemberNames();
    Json::Reader reader;
    for (auto it = mem.begin(); it != mem.end(); it++)
    {
        std::string testName;
        LOG(INFO) << " " << *it;
        testName = "[" + *it + "]";
        Json::Value o = jsonObj[*it];
        if (jsonObj[*it].type() == Json::arrayValue)
        {
            /*BOOST_REQUIRE_MESSAGE(obj.count("out") > 0, testName + " out has not been set !");
            BOOST_REQUIRE_MESSAGE(!obj.at("out").is_null(), testName + " out has been set to null
            !"); BOOST_REQUIRE_MESSAGE(obj.count("in") > 0, testName + " in has not been set !");*/
            Json::Value::Members o_mem = o.getMemberNames();
            for (auto it = o_mem.begin(); it != o_mem.end(); it++)
            {
                Json::Value inObj = o[*it]["in"];
                RlpType rlpType = RlpType::Test;
                if (inObj.type() == Json::stringValue)
                {
                    if (inObj.asString() == "INVALID")
                        rlpType = RlpType::Invalid;
                    else if (inObj.asString() == "VALID")
                        rlpType = RlpType::Valid;
                }
                if (rlpType == RlpType::Test)
                {
                    // check whether "in" transformed rlp string is equal to "out"
                    checkEncode(testName, inObj);
                }
                // check decode
                bool exception = false;
                checkDecode(inObj, rlpType, exception);
                // invalid rlp string && exceptioned
                if (rlpType == RlpType::Invalid && exception)
                    continue;
                if (rlpType == RlpType::Invalid && !exception)
                    BOOST_ERROR(testName + " Expect RLP Exception as rlp is invalid");
                if (exception)  // rlp valid, but trigger exception
                    BOOST_ERROR(testName + " Unexpected RLP Exception as rlp is valid");
            }
        }
    }
}

/*
 * @ function: transform json object to serialized RLPstream
 * @ param: (1) jsonObj: json object to be transformed to RLP
 *          (2) retRlp: serialized rlp stream transformed from json object
 * @ return: retRlp: serialized rlp stream
 */
void RlpTest::buildRLP(Json::Value const& jsonObj, bcos::RLPStream& retRlp)
{
    // array jason
    if (jsonObj.type() == Json::arrayValue)
    {
        bcos::RLPStream item_rlp;
        Json::Value::Members mem = jsonObj.getMemberNames();
        for (auto it = mem.begin(); it != mem.end(); it++)
        {
            BOOST_CHECK(jsonObj[*it].type() == Json::objectValue);
            buildRLP(jsonObj[*it], item_rlp);
        }
        retRlp.appendList(item_rlp.out());
    }
    else if (jsonObj.type() == Json::uintValue)
    {
        retRlp.append(jsonObj.asUInt64());
    }
    else if (jsonObj.type() == Json::stringValue)
    {
        auto str = jsonObj.asString();
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
void RlpTest::checkRlp(Json::Value const& jsonObj, bcos::RLP const& rlp)
{
    if (jsonObj.type() == Json::arrayValue)
    {
        // ##check type
        // must be list
        BOOST_CHECK(rlp.isList());
        // must be not int or data
        BOOST_CHECK(!rlp.isInt());
        BOOST_CHECK(!rlp.isData());
        // compare value
        Json::Value::Members mem = jsonObj.getMemberNames();
        size_t i = 0;
        for (auto it = mem.begin(); it != mem.end(); it++)
        {
            BOOST_CHECK(jsonObj[*it].type() == Json::objectValue);
            checkRlp(jsonObj[*it], rlp[i]);
            i++;
        }
    }
    else if (jsonObj.type() == Json::intValue)
    {
        BOOST_CHECK(rlp.isInt());
        BOOST_CHECK(!rlp.isList());
        BOOST_CHECK(!rlp.isNull());
        BOOST_CHECK(rlp == jsonObj.asInt());
    }
    else if (jsonObj.type() == Json::stringValue)
    {
        std::string str = jsonObj.asString();
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
            BOOST_CHECK(rlp == jsonObj.asString());
        }
    }
}
}  // namespace test
}  // namespace bcos
