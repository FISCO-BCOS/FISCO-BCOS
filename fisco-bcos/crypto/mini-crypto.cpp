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
constexpr char hexmap[] = {'0', '1', '2', '3', '4', '5', '6', '7',
                           '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
std::string hexStr(unsigned char *data, int len)
{
  std::string s(len * 2, ' ');
  for (int i = 0; i < len; ++i) {
    s[2 * i]     = hexmap[(data[i] & 0xF0) >> 4];
    s[2 * i + 1] = hexmap[data[i] & 0x0F];
  }
  return s;
}

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
    std::cout << "****before sdf sm2 sign" << endl;
    std::shared_ptr<crypto::Signature> sdfResult = SDFSM2Sign(kp, h);
    std::cout << ">>> after sdf sm2 sign" << endl;
    std::cout << "*** internal sign result: r = " << sdfResult->r.hex()
              << " s = " << sdfResult->s.hex() << std::endl;
    bool result1 = sm2Verify(kp.pub(), swResult, h);
    cout << "*** soft sign, soft verify :" << result1 << endl;
    bool result2 = sm2Verify(kp.pub(), sdfResult, h);
    cout << "*** hardware sign, soft verify： " << result2 << endl;
    bool result3 = SDFSM2Verify(kp.pub(), sdfResult, h);
    cout << "*** hardware sign, hardware verify: " << result3 << endl;
    bool result4 = SDFSM2Verify(kp.pub(), swResult, h);
    cout << "*** soft sign, hardware verify: " << result4 << endl<< endl;

    cout << "*** soft sm4 enc, hardware decrypt: " << endl;
    const std::string key = "0B780F0F13CE7F3F1383053CAC817ABF";
    //const std::string key = "0c9c5d0f151fe701be2a5d5bfc947a5f118ff4d31dc11850dda5a6e923d5c8a50c9c5d0f151fe701be2a5d5bfc947a5f118ff4d31dc11850dda5a6e923d5c8a50c9c5d0f151fe701be2a5d5bfc947a5f118ff4d31dc11850dda5a6e923d5c8a50c9c5d0f151fe701be2a5d5bfc947a5f118ff4d31dc11850dda5a6e923d5c8a5";
    const std::string plainData =
        "1E59A21C02EBE011A33A36F497A0D5927EE96088EF3232DA09AD25BE3B1F1C9EF24D357E25B4A15472C41682BE"
        "ACE94E";
    const std::string iv = "B47B07085D258F6EE60790A786C7DFBD";
    cout << "plain text: " << hexStr((unsigned char*)plainData.data(),plainData.size()) << endl;
    cout << "plain text len:" << plainData.size() <<endl;
    const std::string endata = sm4Encrypt((const unsigned char*)plainData.data(), plainData.size(),
        (const unsigned char*)key.data(), key.size(), (const unsigned char*)key.data());
    cout << "soft encrypted text: " << hexStr((unsigned char*)endata.data(),endata.size()) << endl;
    cout << "soft encrypted text len:" << endata.size() <<endl;
    const std::string dedata = SDFSM4Decrypt((const unsigned char*)endata.data(), endata.size(),
        (const unsigned char*)key.data(), key.size(), (const unsigned char*)key.data());
    cout << "hardware decrypt text: " << hexStr((unsigned char*)dedata.data(),dedata.size()) << endl;
    cout << "hardware decrypt text len:" << dedata.size() <<endl<< endl;

    cout << "*** soft sm4 enc, software sm4 decrypt: " << endl;
    cout << "plain text: " << hexStr((unsigned char*)plainData.data(),plainData.size()) << endl;
    cout << "plain text len:" << plainData.size() <<endl;
    const std::string sssendata = sm4Encrypt((const unsigned char*)plainData.data(), plainData.size(),
        (const unsigned char*)key.data(), key.size(), (const unsigned char*)key.data());
    cout << "soft encrypted text: " << hexStr((unsigned char*)sssendata.data(),sssendata.size()) << endl;
    cout << "soft encrypted text len:" << sssendata.size() <<endl;
    const std::string sssdedata = sm4Decrypt((const unsigned char*)sssendata.data(), sssendata.size(),
        (const unsigned char*)key.data(), key.size(), (const unsigned char*)key.data());
    cout << "hardware decrypt text: " << hexStr((unsigned char*)sssdedata.data(),sssdedata.size()) << endl;
    cout << "hardware decrypt text len:" << sssdedata.size() <<endl<< endl;


    cout << "*** hardware sm4 enc, hardware decrypt: " << endl;
    cout << "plain text: " << hexStr((unsigned char*)plainData.data(),plainData.size()) << endl;
    cout << "plain text len:" << plainData.size() <<endl;
    const std::string hhendata = SDFSM4Encrypt((const unsigned char*)plainData.data(), plainData.size(),
        (const unsigned char*)key.data(), key.size(), (const unsigned char*)key.data());
    cout << "hardware encrypted text: " << hexStr((unsigned char*)hhendata.data(),hhendata.size()) << endl;
    cout << "hardware encrypted text len:" << hhendata.size() <<endl;
    const std::string hhdedata = SDFSM4Decrypt((const unsigned char*)hhendata.data(), hhendata.size(),
        (const unsigned char*)key.data(), key.size(), (const unsigned char*)key.data());
    cout << "hardware decrypt text: " << hexStr((unsigned char*)hhdedata.data(),hhdedata.size()) << endl;
    cout << "hardware decrypt text len:" << hhdedata.size() <<endl<< endl;
    
    cout << "*** hardware sm4 enc, software decrypt " << endl;
    const std::string sdfendata = SDFSM4Encrypt((const unsigned char*)plainData.data(), plainData.size(),
        (const unsigned char*)key.data(), key.size(), (const unsigned char*)key.data());
    const std::string softdedata = sm4Decrypt((const unsigned char*)sdfendata.data(), sdfendata.size(),
        (const unsigned char*)key.data(), key.size(), (const unsigned char*)key.data());
    cout << "plain text: " << hexStr((unsigned char*)plainData.data(),plainData.size()) << endl;
    cout << "soft encrypted text: " << hexStr((unsigned char*)sdfendata.data(),sdfendata.size()) << endl;
    cout << "hardware decrypt text: " << hexStr((unsigned char*)softdedata.data(),softdedata.size()) << endl<< endl;



    std::cout << "*** internal key sign and verify" << std::endl;
    KeyPair keyPair2 = KeyPair::create();
    keyPair2.setKeyIndex(1);
    std::shared_ptr<crypto::Signature> sdfInternalSignResult = SDFSM2Sign(keyPair2, h);
    std::cout << "*** internal sign result: r = " << sdfInternalSignResult->r.hex()
              << " s = " << sdfInternalSignResult->s.hex() << std::endl;

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
