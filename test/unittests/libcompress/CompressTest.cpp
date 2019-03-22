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
 * @brief: UT for SnappyCompress and LZ4Compress
 *
 * @file CompressTest.cpp
 * @author: yujiechen
 * @date 2019-03-21
 */

#include <libcompress/LZ4Compress.h>
#include <libcompress/SnappyCompress.h>
#include <libdevcore/CommonData.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <iostream>

using namespace dev;
using namespace dev::compress;
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(CompressTest, TestOutputHelperFixture)

class CompressTestFixture
{
public:
    void checkCompressAlgorithm(std::string algorithm)
    {
        size_t dataLenFields = 0;
        if (algorithm == "snappy")
        {
            m_compressHandler = std::make_shared<SnappyCompress>();
        }
        else
        {
            m_compressHandler = std::make_shared<LZ4Compress>();
            dataLenFields = sizeof(size_t) / sizeof(char);
        }

        std::string inputData = "abc21324";
        bytes compressedData;
        bytes inputDataBytes(inputData.begin(), inputData.end());

        /// compress test
        size_t compressLen = m_compressHandler->compress(ref(inputDataBytes), compressedData);
        BOOST_CHECK((compressLen + dataLenFields) == compressedData.size());

        /// uncompress test
        bytes uncompressData;
        size_t uncompressLen = m_compressHandler->uncompress(ref(compressedData), uncompressData);
        BOOST_CHECK(uncompressData.size() == uncompressLen);
        BOOST_CHECK(toHex(uncompressData) == toHex(inputDataBytes));
        BOOST_CHECK(asString(uncompressData) == inputData);

        /// compress with offset
        bytes compressedData2;
        compressedData2.push_back(2);
        size_t compressLen2 = m_compressHandler->compress(ref(inputDataBytes), compressedData2, 1);
        BOOST_CHECK(compressLen2 == compressLen);
        BOOST_CHECK(compressedData2[0] == 2);
        BOOST_CHECK(compressedData2.size() == (compressLen2 + 1 + dataLenFields));

        size_t uncompressLen2 =
            m_compressHandler->uncompress(ref(compressedData2).cropped(1), uncompressData);
        BOOST_CHECK(uncompressLen == uncompressLen2);
        BOOST_CHECK(uncompressData.size() == uncompressLen2);
        BOOST_CHECK(uncompressData == inputDataBytes);
        BOOST_CHECK(asString(uncompressData) == asString(inputDataBytes));
    }

private:
    std::shared_ptr<CompressInterface> m_compressHandler;
};

BOOST_AUTO_TEST_CASE(testCompressAlgorithm)
{
    CompressTestFixture compressFixture;
    compressFixture.checkCompressAlgorithm("snappy");
    compressFixture.checkCompressAlgorithm("lz4");
}

BOOST_AUTO_TEST_CASE(testCompressStatistic)
{
    std::shared_ptr<CompressStatistic> statisticHandler = std::make_shared<CompressStatistic>();
    statisticHandler->setSealerSize(4);
    BOOST_CHECK(statisticHandler->getBroadcastSize() == 3);

    /// update and check the compress value
    uint64_t orgDataSize = 1000;
    uint64_t compressedDataSize = 500;
    uint64_t compressTime = 600;
    statisticHandler->updateCompressValue(orgDataSize, compressedDataSize, compressTime, false);
    BOOST_CHECK(statisticHandler->orgCompressDataSize() == orgDataSize);
    BOOST_CHECK(statisticHandler->compressDataSize() == compressedDataSize);
    BOOST_CHECK(statisticHandler->compressTime() == compressTime);
    BOOST_CHECK(statisticHandler->sendDataSize() == statisticHandler->compressDataSize());
    BOOST_CHECK(statisticHandler->savedSendDataSize() == (orgDataSize - compressedDataSize));

    /// update compressValue with broadcast option
    statisticHandler->updateCompressValue(orgDataSize, compressedDataSize, compressTime, true);
    BOOST_CHECK(statisticHandler->orgCompressDataSize() == 2 * orgDataSize);
    BOOST_CHECK(statisticHandler->compressDataSize() == 2 * compressedDataSize);
    BOOST_CHECK(statisticHandler->compressTime() == 2 * compressTime);
    BOOST_CHECK(statisticHandler->sendDataSize() ==
                (statisticHandler->getBroadcastSize() + 1) * compressedDataSize);
    BOOST_CHECK(statisticHandler->savedSendDataSize() ==
                (statisticHandler->getBroadcastSize() + 1) * (orgDataSize - compressedDataSize));

    /// update uncompressValue
    uint64_t uncompressTime = 300;
    size_t freq = 2;
    for (size_t i = 0; i < freq; i++)
    {
        statisticHandler->updateUncompressValue(compressedDataSize, orgDataSize, uncompressTime);
    }
    BOOST_CHECK(statisticHandler->orgUncompressDataSize() == freq * compressedDataSize);
    BOOST_CHECK(statisticHandler->uncompressDataSize() == freq * orgDataSize);
    BOOST_CHECK(statisticHandler->uncompressTime() == freq * uncompressTime);
    BOOST_CHECK(statisticHandler->recvDataSize() == freq * compressedDataSize);
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
