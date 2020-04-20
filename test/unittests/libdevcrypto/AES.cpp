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

#include <libdevcore/Assertions.h>
#include <libdevcore/CommonJS.h>
#include <libdevcrypto/AES.h>
// #include <libdevcrypto/Hash.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <string>


using namespace dev;
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(AES, TestOutputHelperFixture)
#ifdef FISCO_GM
BOOST_AUTO_TEST_CASE(GM_testAESBytes)
{
    bytes key = fromHex("0B780F0F13CE7F3F1383053CAC817ABF");
    bytes plainData = fromHex(
        "1E59A21C02EBE011A33A36F497A0D5927EE96088EF3232DA09AD25BE3B1F1C9EF24D357E25B4A15472C41682BE"
        "ACE94E");
    bytes iv = fromHex("B47B07085D258F6EE60790A786C7DFBD");
    bytes endata = aesCBCEncrypt(&plainData, &key, &iv);
    BOOST_CHECK_EQUAL(toHex(endata),
        "8cde530f43b5706162b829a45773ed4d5ddaab3dc065310f5a621b03a5c2a1fe2fc8442feebf5a32a4ff62933d"
        "2d254c459f6c3bdd3a56c83eee906f4a059b22");
    bytes dedata = aesCBCDecrypt(&endata, &key, &iv);
    BOOST_CHECK_EQUAL(toHex(plainData), toHex(dedata));

    bytes keyWithoutIv = fromHex("00000000000000000000000000000000");
    bytes plainDataWithoutIv = fromHex(
        "5942D6DA31CD0A93E46CF382468710888F4393C6D734B3C6C6C44B1F6F34B08AEE0386B91831A268C4E9815BB6"
        "1375F4CDA913BA80C37CE6F4971977319CCD7D23502328A45130D0FDF5B63A77EA601F806733FBCADF969B08AA"
        "9AA56A8B509FAA7E95FC3706E3482EF1532A91DB2EB3EDF234D1E2E57F75B5EACC81A391");
    bytes endataWithoutIv = aesCBCEncrypt(&plainDataWithoutIv, &keyWithoutIv);
    BOOST_CHECK_EQUAL(toHex(endataWithoutIv),
        "722a8ee5e381215faf16d619e49d7f8424016a3c6e59060c7bb5af4e795984877cde45fa8828bfc6927dc1cc0e"
        "0be687d741e8a079cdec9eb09a3db5f0323ee93bd3f37557e5554935e3e3bf9851c931fefd5b9b5212cbc5ec9e"
        "8d9c901baf292de43a3057b6c100a8f559bc9f60ec8fe424fc381bc1f405d885b12b493da18d");
    bytes dedataWithoutIv = aesCBCDecrypt(&endataWithoutIv, &keyWithoutIv);
    BOOST_CHECK_EQUAL(toHex(plainDataWithoutIv), toHex(dedataWithoutIv));
}

BOOST_AUTO_TEST_CASE(GM_testAESString)
{
    const std::string key = "0B780F0F13CE7F3F1383053CAC817ABF";
    const std::string plainData =
        "1E59A21C02EBE011A33A36F497A0D5927EE96088EF3232DA09AD25BE3B1F1C9EF24D357E25B4A15472C41682BE"
        "ACE94E";
    const std::string iv = "B47B07085D258F6EE60790A786C7DFBD";
    const std::string endata = aesCBCEncrypt(plainData, key, iv);
    const std::string dedata = aesCBCDecrypt(endata, key, iv);

    BOOST_CHECK_EQUAL(plainData, dedata);

    const std::string keyWithoutIv = "00000000000000000000000000000000";
    const std::string plainDataWithoutIv =
        "5942D6DA31CD0A93E46CF382468710888F4393C6D734B3C6C6C44B1F6F34B08AEE0386B91831A268C4E9815BB6"
        "1375F4CDA913BA80C37CE6F4971977319CCD7D23502328A45130D0FDF5B63A77EA601F806733FBCADF969B08AA"
        "9AA56A8B509FAA7E95FC3706E3482EF1532A91DB2EB3EDF234D1E2E57F75B5EACC81A391";
    const std::string endataWithoutIv = aesCBCEncrypt(plainDataWithoutIv, keyWithoutIv);
    const std::string dedataWithoutIv = aesCBCDecrypt(endataWithoutIv, keyWithoutIv);

    BOOST_CHECK_EQUAL(plainDataWithoutIv, dedataWithoutIv);
}
#else
BOOST_AUTO_TEST_CASE(testAESBytes)
{
    bytes key = fromHex("1527F8068E8F577F4CBB38CB8206CEEA");
    bytes plainData = fromHex(
        "8398a06d3a238e77efdf91e5ce5596942c42788065d24a8dfaf911501be71181a9d0a318a19d5f7b087401233f"
        "c937a5");
    bytes iv = fromHex("081199C91FA3A8F0B31F7EAB053B770F");
    bytes endata = aesCBCEncrypt(&plainData, &key, &iv);
    BOOST_CHECK_EQUAL(toHex(endata),
        "1372c3bdad84fe82ff1dd238ff54c623513c409dd6f65645648657bd287c688a294932aad9ae6db0d0d791320f"
        "24bc15170bdc58338c2446b940970d5653be4f");
    bytes dedata = aesCBCDecrypt(&endata, &key, &iv);
    BOOST_CHECK_EQUAL(toHex(plainData), toHex(dedata));

    bytes keyWithoutIv = fromHex("00000000000000000000000000000000");
    bytes plainDataWithoutIv = fromHex("2b731e559c35eb31ad86ea0eaa441f19");
    bytes endataWithoutIv = aesCBCEncrypt(&plainDataWithoutIv, &keyWithoutIv);
    BOOST_CHECK_EQUAL(
        toHex(endataWithoutIv), "303db2ab90477d21c489867bfaea1a90b77eaf4134d8b388fdfb834b405ac580");
    bytes dedataWithoutIv = aesCBCDecrypt(&endataWithoutIv, &keyWithoutIv);
    BOOST_CHECK_EQUAL(toHex(plainDataWithoutIv), toHex(dedataWithoutIv));
}

BOOST_AUTO_TEST_CASE(testAESString)
{
    const std::string key = "1527F8068E8F577F4CBB38CB8206CEEA";
    const std::string plainData =
        "8398A06D3A238E77EFDF91E5CE5596942C42788065D24A8DFAF911501BE71181A9D0A318A19D5F7B087401233F"
        "C937A5";
    const std::string iv = "081199C91FA3A8F0B31F7EAB053B770F";
    const std::string endata = aesCBCEncrypt(plainData, key, iv);
    const std::string dedata = aesCBCDecrypt(endata, key, iv);

    BOOST_CHECK_EQUAL(plainData, dedata);

    const std::string keyWithoutIv = "00000000000000000000000000000000";
    const std::string plainDataWithoutIv = "2B731E559C35EB31AD86EA0EAA441F19";
    const std::string endataWithoutIv = aesCBCEncrypt(plainDataWithoutIv, keyWithoutIv);
    const std::string dedataWithoutIv = aesCBCDecrypt(endataWithoutIv, keyWithoutIv);

    BOOST_CHECK_EQUAL(plainDataWithoutIv, dedataWithoutIv);
}
#endif
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev