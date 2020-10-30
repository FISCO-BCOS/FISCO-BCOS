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
BOOST_AUTO_TEST_CASE(testTxRlp)
{
    std::string txRlpString =
        "f8ae9f3446824a94c2461d08bd9f4dbb583e6f066fb8ae67c5b6e2933020b41f19c185051f4d5c0083419ce082"
        "026994d1b50090cf3c7588359a670b775546a72a2be95480a466c9913900000000000000000000000000000000"
        "000000000000000000000000000000040101801ba03faa8232ad248fc31c68eb56e8bbedf4e70be9381460afd9"
        "9819b8cf8b6c91cba052121430a55f5ec0c2a2db3000324335507a46b697f961bb86be2687d0d8caf9";
    dev::bytes rlpBytes = dev::fromHex(txRlpString);
    bytesConstRef rlpData = ref(rlpBytes);
    RLP const rlp(rlpData);
    dev::h256 r = rlp[11].toInt<u256>();
    std::cout << "decoded r:" << toHex(r);
    BOOST_CHECK(
        dev::toHex(r) == "3faa8232ad248fc31c68eb56e8bbedf4e70be9381460afd99819b8cf8b6c91cb");
}

BOOST_AUTO_TEST_SUITE_END()
