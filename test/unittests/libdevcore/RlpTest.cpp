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
 * @file RlpTest.cpp
 * @author: yujiechen
 * @date 2018-08-24
 */

#include "rlp.h"
#include <test/tools/libutils/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>

#include <libdevcore/Common.h>
#include <libdevcore/CommonData.h>
#include <libdevcore/Exceptions.h>
#include <libdevcore/RLP.h>

using namespace std;
BOOST_FIXTURE_TEST_SUITE(RlpTests, dev::test::TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(EmptyArrayList)
{
    try
    {
        dev::bytes payloadToDecode = dev::fromHex("80");
        dev::RLP payload(payloadToDecode);
        LOG(INFO) << payload;

        payloadToDecode = dev::fromHex("c0");
        dev::RLP payload2(payloadToDecode);
        LOG(INFO) << payload2;
    }
    catch (dev::Exception const& _e)
    {
        BOOST_ERROR("(EmptyArrayList) Failed test with Exception: " << _e.what());
    }
    catch (std::exception const& _e)
    {
        BOOST_ERROR("(EmptyArrayList) Failed test with Exception: " << _e.what());
    }
}

BOOST_AUTO_TEST_CASE(invalidRLPtest)
{
    dev::test::RlpTest::runRlpTest("invalidRLPTest", "/RLPTests");
}

BOOST_AUTO_TEST_CASE(rlptest)
{
    dev::test::RlpTest::runRlpTest("rlptest", "/RLPTests");
}

BOOST_AUTO_TEST_CASE(rlpRandom)
{
    fs::path testPath = dev::test::getTestPath();
    testPath /= fs::path("RLPTests/RandomRLPTests");
    std::vector<fs::path> testFiles = dev::test::getFiles(testPath, {".json"});
    for (auto& path : testFiles)
    {
        try
        {
            LOG(INFO) << "Testing ... " << path.filename();
            std::string s = dev::asString(dev::contents(path.string()));
            std::string empty_str = "Content of " + path.string() + " is empty";
            BOOST_REQUIRE_MESSAGE(s.length() > 0, empty_str);
            Json::Value v;
            Json::Reader reader;
            reader.parse(s, v);
            dev::test::RlpTest::doRlpTests(v);
        }
        catch (std::exception const& _e)
        {
            BOOST_ERROR(path.filename().string() + "Failed test with Exception: " << _e.what());
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
