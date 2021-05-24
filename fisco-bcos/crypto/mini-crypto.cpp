/*
    This file is part of fisco-bcos.

    fisco-bcos is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    fisco-bcos is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with fisco-bcos.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file: mini-crypto.cpp
 * @author: yujiechen
 *
 * @date: 2021
 */

#include <libdevcrypto/Common.h>
#include <libdevcrypto/CryptoInterface.h>
#include <libdevcrypto/Hash.h>
#include <libdevcrypto/SM2Signature.h>
#include <libdevcrypto/SM3Hash.h>
#include <libdevcrypto/SM4Crypto.h>
#include <libdevcrypto/hsm/HSMCrypto.h>
#include <libdevcrypto/hsm/HSMHash.h>
#include <libdevcrypto/hsm/HSMSignature.h>
using namespace dev::crypto;
using namespace dev;
int main(int, const char* argv[])
{
    std::cout << "#### begin hsm test" << std::endl;
    size_t loopRound = atoi(argv[1]);
    initSMCrypto();
    g_BCOSConfig.setUseSMCrypto(true);
    KeyPair keyPair = KeyPair::create();

    std::cout << "#### begin function test" << std::endl;
    KeyPair kp = KeyPair::create();
    std::string pubHex = toHex(kp.pub().data(), kp.pub().data() + 64, "04");
    h256 h(dev::fromHex("0x68b5bae5fe19851624298fd1e9b4d788627ac27c13aad3240102ffd292a17911"));
    std::shared_ptr<crypto::Signature> swResult = sm2Sign(kp, h);
    std::cout << "****before sdf sm2 sign"<<endl;
    std::shared_ptr<crypto::Signature> sdfResult = SDFSM2Sign(kp, h);
    std::cout << ">>> after sdf sm2 sign"<<endl;
    std::cout << "*** internal sign result: r = "<< sdfResult->r.hex() << " s = "<< sdfResult->s.hex()<<std::endl;
    bool result1 = sm2Verify(kp.pub(), swResult, h);
    cout << "*** soft sign, soft verify :" << result1 << endl;
    bool result2 = sm2Verify(kp.pub(), sdfResult, h);
    cout << "*** hardware sign, soft verify： " << result2 << endl;
    bool result3 = SDFSM2Verify(kp.pub(), sdfResult, h);
    cout << "*** hardware sign, hardware verify: " << result3 << endl;
    bool result4 = SDFSM2Verify(kp.pub(), swResult, h);
    cout << "*** soft sign, hardware verify: " << result4 << endl;


    const std::string key = "0B780F0F13CE7F3F1383053CAC817ABF";
    const std::string plainData =
        "1E59A21C02EBE011A33A36F497A0D5927EE96088EF3232DA09AD25BE3B1F1C9EF24D357E25B4A15472C41682BE"
        "ACE94E";
    const std::string iv = "B47B07085D258F6EE60790A786C7DFBD";
    const std::string endata = sm4Encrypt((const unsigned char*)plainData.data(), plainData.size(),
        (const unsigned char*)key.data(), key.size(), (const unsigned char*)iv.data());

    const std::string dedata = SDFSM4Decrypt((const unsigned char*)endata.data(), endata.size(),
        (const unsigned char*)key.data(), key.size(), (const unsigned char*)iv.data());
    int softHardSM4 = plainData.compare(dedata);
    cout << "*** soft sm4 enc, hardware decrypt: " << softHardSM4 << endl;


    // const std::string dedata = sm4Decrypt((const unsigned char*)endata.data(),
    // endata.size(),(const unsigned char*)key.data(), key.size(), (const unsigned char*)iv.data());
    // BOOST_CHECK_EQUAL(plainData, dedata);

    const std::string keyWithoutIv = "00000000000000000000000000000000";
    const std::string plainDataWithoutIv =
        "5942D6DA31CD0A93E46CF382468710888F4393C6D734B3C6C6C44B1F6F34B08AEE0386B91831A268C4E9815BB6"
        "1375F4CDA913BA80C37CE6F4971977319CCD7D23502328A45130D0FDF5B63A77EA601F806733FBCADF969B08AA"
        "9AA56A8B509FAA7E95FC3706E3482EF1532A91DB2EB3EDF234D1E2E57F75B5EACC81A391";
    const std::string endataWithoutIv = sm4Encrypt((const unsigned char*)plainDataWithoutIv.data(),
        plainDataWithoutIv.size(), (const unsigned char*)keyWithoutIv.data(), keyWithoutIv.size(),
        (const unsigned char*)keyWithoutIv.data());
    const std::string dedataWithoutIv = SDFSM4Decrypt((const unsigned char*)endataWithoutIv.data(),
        endataWithoutIv.size(), (const unsigned char*)keyWithoutIv.data(), endataWithoutIv.size(),
        (const unsigned char*)keyWithoutIv.data());

    int softHardSM4WithoutIv = plainDataWithoutIv.compare(dedataWithoutIv);
    cout << "*** soft sm4 enc, hardware decrypt without iv: " << softHardSM4WithoutIv << endl;

    std::cout << "*** internal key sign and verify" << std::endl;
        KeyPair keyPair2 = KeyPair::create();
	    keyPair2.setKeyIndex(1);
	        std::shared_ptr<crypto::Signature> sdfInternalSignResult = SDFSM2Sign(keyPair2, h);
		    std::cout << "*** internal sign result: r = "<< sdfInternalSignResult->r.hex() << " s = "<< sdfInternalSignResult->s.hex()<<std::endl;
    getchar();
    std::cout << "#### begin performance test" << std::endl;

    // calculate hash
    std::cout << "### test sm3" << std::endl;
    clock_t start = clock();
    std::string input = "test_sm3";
    for (size_t i = 0; i < loopRound; i++)
    {
        sm3(input);
    }
    clock_t end = clock();
    std::cout << "Number of calculate round: " << loopRound
              << ",  duration(s) : " << (double)(end - start) / CLOCKS_PER_SEC << endl;
    std::cout << "Times per second: " << loopRound / ((double)(end - start) / CLOCKS_PER_SEC)
              << endl
              << endl;


    std::cout << "### test SDF sm3" << std::endl;
    start = clock();
    for (size_t i = 0; i < loopRound; i++)
    {
        SDFSM3(input);
    }
    end = clock();
    std::cout << "Number of calculate round: " << loopRound
              << ",  duration(s) : " << (double)(end - start) / CLOCKS_PER_SEC << endl;
    std::cout << "Times per second: " << loopRound / ((double)(end - start) / CLOCKS_PER_SEC)
              << endl
              << endl;

    std::cout << "### test sm2 sign" << std::endl;
    auto hash = sm3(input);
    start = clock();
    for (size_t i = 0; i < loopRound; i++)
    {
        sm2Sign(keyPair, hash);
    }
    end = clock();
    std::cout << "Number of calculate round: " << loopRound
              << ",  duration(s) : " << (double)(end - start) / CLOCKS_PER_SEC << endl;
    std::cout << "Times per second: " << loopRound / ((double)(end - start) / CLOCKS_PER_SEC)
              << endl
              << endl;

    std::cout << "### test SDF sm2 sign" << std::endl;
    start = clock();
    for (size_t i = 0; i < loopRound; i++)
    {
        SDFSM2Sign(keyPair, hash);
    }
    end = clock();
    std::cout << "Number of calculate round: " << loopRound
              << ",  duration(s) : " << (double)(end - start) / CLOCKS_PER_SEC << endl;
    std::cout << "Times per second: " << loopRound / ((double)(end - start) / CLOCKS_PER_SEC)
              << endl
              << endl;

    auto signatureResult = sm2Sign(keyPair, hash);
    std::cout << "### test sm2 verify" << std::endl;
    start = clock();
    for (size_t i = 0; i < loopRound; i++)
    {
        sm2Verify(keyPair.pub(), signatureResult, hash);
    }
    end = clock();
    std::cout << "Number of calculate round: " << loopRound
              << ",  duration(s) : " << (double)(end - start) / CLOCKS_PER_SEC << endl;
    std::cout << "Times per second: " << loopRound / ((double)(end - start) / CLOCKS_PER_SEC)
              << endl
              << endl;

    std::cout << "### test SDF sm2 verify" << std::endl;
    start = clock();
    for (size_t i = 0; i < loopRound; i++)
    {
        SDFSM2Verify(keyPair.pub(), signatureResult, hash);
    }
    end = clock();
    std::cout << "Number of calculate round: " << loopRound
              << ",  duration(s) : " << (double)(end - start) / CLOCKS_PER_SEC << endl;
    std::cout << "Times per second: " << loopRound / ((double)(end - start) / CLOCKS_PER_SEC)
              << endl
              << endl;
    std::cout << "#### test end" << std::endl;
    getchar();
}
