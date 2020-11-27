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
 * @brief Unit tests for the Hash
 * @file Hash.cpp
 * @author: chaychen asherli
 * @date 2018
 */
#include <iostream>

#include <libdevcrypto/SM4Crypto.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <string>


using namespace bcos;
using namespace bcos::crypto;
namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(SM4, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(testSM4String)
{
    const std::string key = "0B780F0F13CE7F3F1383053CAC817ABF";
    const std::string plainData =
        "1E59A21C02EBE011A33A36F497A0D5927EE96088EF3232DA09AD25BE3B1F1C9EF24D357E25B4A15472C41682BE"
        "ACE94E";
    const std::string iv = "B47B07085D258F6EE60790A786C7DFBD";
    const std::string endata = sm4Encrypt((const unsigned char*)plainData.data(), plainData.size(),
        (const unsigned char*)key.data(), key.size(), (const unsigned char*)iv.data());
    const std::string dedata = sm4Decrypt((const unsigned char*)endata.data(), endata.size(),
        (const unsigned char*)key.data(), key.size(), (const unsigned char*)iv.data());

    BOOST_CHECK_EQUAL(plainData, dedata);

    const std::string keyWithoutIv = "00000000000000000000000000000000";
    const std::string plainDataWithoutIv =
        "5942D6DA31CD0A93E46CF382468710888F4393C6D734B3C6C6C44B1F6F34B08AEE0386B91831A268C4E9815BB6"
        "1375F4CDA913BA80C37CE6F4971977319CCD7D23502328A45130D0FDF5B63A77EA601F806733FBCADF969B08AA"
        "9AA56A8B509FAA7E95FC3706E3482EF1532A91DB2EB3EDF234D1E2E57F75B5EACC81A391";
    const std::string endataWithoutIv = sm4Encrypt((const unsigned char*)plainDataWithoutIv.data(),
        plainDataWithoutIv.size(), (const unsigned char*)keyWithoutIv.data(), keyWithoutIv.size(),
        (const unsigned char*)keyWithoutIv.data());
    const std::string dedataWithoutIv = sm4Decrypt((const unsigned char*)endataWithoutIv.data(),
        endataWithoutIv.size(), (const unsigned char*)keyWithoutIv.data(), endataWithoutIv.size(),
        (const unsigned char*)keyWithoutIv.data());

    BOOST_CHECK_EQUAL(plainDataWithoutIv, dedataWithoutIv);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
