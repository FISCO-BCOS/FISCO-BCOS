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
#include <libdevcrypto/sdf/SDFSM2Signature.h>
#include <libdevcrypto/sdf/SDFSM3Hash.h>
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
    h256 h(fromHex("0x68b5bae5fe19851624298fd1e9b4d788627ac27c13aad3240102ffd292a17911"));
    std::shared_ptr<crypto::Signature> swResult = sm2Sign(kp,h);
    std::shared_ptr<crypto::Signature>  sdfResult = SDFSM2Sign(kp,h);	
    bool result1 = sm2Verify(kp.pub(),swResult,h);
    cout<< "*** soft sign, soft verify :"<< result1 <<endl <<endl;
    bool result2 = sm2Verify(kp.pub(),sdfResult,h);
    cout<<"*** hardware sign, soft verify： "<< result2 <<endl;
    bool result3 = SDFSM2Verify(kp.pub(),sdfResult,h);
    cout <<"*** hardware sign, hardware verify: "<< result3 <<endl;
    bool result4 = SDFSM2Verify(kp.pub(),swResult,h);
    cout <<"*** soft sign, hardware verify: "<< result4 <<endl;
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
    std::cout << "Times per second: " << loopRound / ((double)(end - start) / CLOCKS_PER_SEC) << endl
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
    std::cout << "Times per second: " << loopRound / ((double)(end - start) / CLOCKS_PER_SEC) << endl
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
    std::cout << "Times per second: " << loopRound / ((double)(end - start) / CLOCKS_PER_SEC) << endl
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
    std::cout << "Times per second: " << loopRound / ((double)(end - start) / CLOCKS_PER_SEC) << endl
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
    std::cout << "Times per second: " << loopRound / ((double)(end - start) / CLOCKS_PER_SEC) << endl
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
    std::cout << "Times per second: " << loopRound / ((double)(end - start) / CLOCKS_PER_SEC) << endl
              << endl;
    std::cout << "#### test end" << std::endl;
    getchar();
}

