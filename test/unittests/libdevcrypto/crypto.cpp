/*
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
 */
/**
 * @brief unit test for CryptoPP.
 *
 * @file crypto.cpp
 * @author yujiechen
 * @date 2018-08-28
 */

#include <libdevcrypto/Common.h>
#include <libdevcrypto/CryptoPP.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
using namespace dev;
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(CryptoPPTest, TestOutputHelperFixture)
// test common encryption and decryption
#ifndef FISCO_GM
BOOST_AUTO_TEST_CASE(testCommonEncAndDec)
{
    // check single pattern
    BOOST_CHECK(dev::crypto::Secp256k1PP::get() == dev::crypto::Secp256k1PP::get());
    std::string plain_text = "abcdlkjwer";
    KeyPair k = KeyPair::create();
    bytes ciper(plain_text.begin(), plain_text.end());
    // callback encrypt
    dev::crypto::Secp256k1PP::get()->encrypt(k.pub(), ciper);
    // callback decrypt
    dev::crypto::Secp256k1PP::get()->decrypt(k.secret(), ciper);
    BOOST_CHECK(asString(ref(ciper)) == plain_text);

    // check invalid decrypt
    std::string ciper_text = "abcdlkjsd";
    bytes invalid_ciper(ciper_text.begin(), ciper_text.end());
    dev::crypto::Secp256k1PP::get()->decrypt(k.secret(), invalid_ciper);
    BOOST_CHECK(invalid_ciper.size() == 0);

    // check invalid secret key
    dev::Secret invalid_key("0x12312");
    ciper_text =
        "04cb89c116deea7e62430397f1a95e9200718327ee272357d316344d\
        3658c25b8fd293b5791610fef3e3302da62471e13b6457dc00ecec604e\
        db1ec7f2e6c338b813a5a0abe2e27b8650a5a26215b9acaccbaaef42b3d\
        729e0a7e1b8b4c346";
    bytes invalid_ciper2 = fromHex(ciper_text);
    dev::crypto::Secp256k1PP::get()->decrypt(invalid_key, invalid_ciper2);
    BOOST_CHECK(invalid_ciper2.size() == 0);
}

// test ECIES Encryption and Decryption
BOOST_AUTO_TEST_CASE(testECIESEncAndDec)
{
    // check encryptECIES(w/AES128-CTR-SHA256)
    std::string plain = "223432lk4jaksfj";
    KeyPair k = KeyPair::create();
    bytes ret_ciper(plain.begin(), plain.end());
    dev::crypto::Secp256k1PP::get()->encryptECIES(k.pub(), ret_ciper);
    BOOST_CHECK(ret_ciper.size() > 0);
    // check decryptECIES
    bool result = dev::crypto::Secp256k1PP::get()->decryptECIES(k.secret(), ret_ciper);
    BOOST_CHECK(asString(ref(ret_ciper)) == plain);
    BOOST_CHECK(result == true);

    // check invalid decrypt(invalid ciper)
    std::string invalid_ciper_text = "lkdjfsdf";
    bytes invalid_ciper(invalid_ciper_text.begin(), invalid_ciper_text.end());
    result = dev::crypto::Secp256k1PP::get()->decryptECIES(k.secret(), invalid_ciper);
    BOOST_CHECK(result == false);
    // check invalid decrypt(invalid key)
    std::string valid_ciper_str =
        "04505a96623681829868456bc646c2dc9b7b0601aa14d7\
        f0f68e0698f440ce973a3bedfd39fd63aaeca8a7357cbd98\
        dc3020b30090e747d72be1e0ea13927cb2ede97965cd3c553\
        d7befec806d904ada89aafc7442371ef79ba8d6540ca4b7e18\
        6678f6371b6e7b784f648498c8b536b14cd51fa78d887879d44162f13597de8";
    bytes valid_ret_ciper = fromHex(valid_ciper_str);
    dev::Secret invalid_secret("32432432432");
    result = dev::crypto::Secp256k1PP::get()->decryptECIES(invalid_secret, valid_ret_ciper);
    BOOST_CHECK(result == false);
}

// test ECIESWithMacData
BOOST_AUTO_TEST_CASE(testECIESWithMacData)
{
    // normal path test
    std::string plain = "23k4sabskdjflsdrew";
    std::string mac_str = "0xe3754bde80a9a351279844914e71b35e80abe5122a78f826f657f0f094a0a8cb";
    bytes mac_bytes = fromHex(mac_str);
    bytes ret_ciper(plain.begin(), plain.end());
    KeyPair k = KeyPair::create();
    // enc
    dev::crypto::Secp256k1PP::get()->encryptECIES(k.pub(), ref(mac_bytes), ret_ciper);
    // std::cout << "ENC ciper ECIESWithMac:" << toHex(ret_ciper) << std::endl;
    BOOST_CHECK(ret_ciper.size() > 0);
    // dec
    bool result =
        dev::crypto::Secp256k1PP::get()->decryptECIES(k.secret(), ref(mac_bytes), ret_ciper);
    // std::cout<<"decrypted plain ECIESWithMac:"<< asString()<<
    BOOST_CHECK(result == true && asString(ref(ret_ciper)) == plain);

    // check invalid decrypt: invalid ciper test
    std::string invalid_ciper_str = "dskj3434sdfds";
    bytes invalid_ciper(invalid_ciper_str.begin(), invalid_ciper_str.end());
    result =
        dev::crypto::Secp256k1PP::get()->decryptECIES(k.secret(), ref(mac_bytes), invalid_ciper);
    BOOST_CHECK(result == false);
    // check invalid decrypt: invalid secret
    std::string valid_ciper_str =
        "0408a447125bdef8268cf39b919365d9\
    742de73140be695c107e28284a8bae59185c6417813905ea05c52db4efddedec\
    a862b4640a5e569c7198ab140f20f4c05590e5e002af0dc67f94950aa346b1e18\
    0e67ec56e87a1f4d9804a2c8b8806c55ac4991ab72d5f6e3686029380e363282b\
    e812b1fe16bb1d68a2641d91493499f51e80";
    bytes valid_ciper = fromHex(valid_ciper_str);
    Secret invalid_sec("23432");
    result =
        dev::crypto::Secp256k1PP::get()->decryptECIES(invalid_sec, ref(mac_bytes), valid_ciper);
    BOOST_CHECK(result == false);
    // check invalid decrypt: invalid mac
    std::string invalid_mac_str =
        "05b28595588d628540022456f90aa0e0\
    8a7f8ef91febff34405bb3890afc9659";
    bytes invalid_mac = fromHex(invalid_mac_str);
    result =
        dev::crypto::Secp256k1PP::get()->decryptECIES(k.secret(), ref(invalid_mac), valid_ciper);
    BOOST_CHECK(result == false);
}

// void Secp256k1PP::agree(Secret const& _s, Public const& _r, Secret& o_s)
BOOST_AUTO_TEST_CASE(testAgree)
{
    KeyPair alice_key = KeyPair::create();
    KeyPair bob_key = KeyPair::create();
    Secret alice_ciper;
    dev::crypto::Secp256k1PP::get()->agree(alice_key.secret(), bob_key.pub(), alice_ciper);
    Secret bob_ciper;
    dev::crypto::Secp256k1PP::get()->agree(bob_key.secret(), alice_key.pub(), bob_ciper);
    BOOST_CHECK(alice_ciper == bob_ciper);

    // exception test
    dev::crypto::Secp256k1PP::get()->agree(alice_key.secret(), alice_key.pub(), alice_ciper);
    dev::crypto::Secp256k1PP::get()->agree(bob_key.secret(), bob_key.pub(), bob_ciper);
    BOOST_CHECK(alice_ciper != bob_ciper);
}
#endif
BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace dev
