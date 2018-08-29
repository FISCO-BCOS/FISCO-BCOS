/*
    @CopyRight:
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @brief unit test for Common.* of devcrypto module
 *
 * @file Common.cpp
 * @author yujiechen
 * @date 2018-08-28
 */
#include <libdevcore/SHA3.h>
#include <libdevcrypto/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
using namespace dev;
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(DevcryptoCommonTest, TestOutputHelperFixture)
/// test toPublic && toAddress
BOOST_AUTO_TEST_CASE(testCommonTrans)
{
    BOOST_CHECK(Secret::size == 32);
    BOOST_CHECK(Public::size == 64);
    BOOST_CHECK(Signature::size == 65);
    // check secret->public
    Secret sec1(
        "bcec428d5205abe0f0cc8a7340839"
        "08d9eb8563e31f943d760786edf42ad67dd");
    Public pub1 = toPublic(sec1);
    // std::cout << "public key:" << toHex(pub1) << std::endl;
    Secret sec2("bcec428d5205abe0");
    Public pub2 = toPublic(sec2);
    // std::cout << "public key:" << toHex(pub1) << std::endl;
    BOOST_CHECK(pub1 != pub2);
    // check public->address
    Address addr_pub1 = toAddress(pub1);
    Address addr_pub2 = toAddress(pub2);
    BOOST_CHECK(addr_pub1 != addr_pub2);
    // check secret->address
    Address addr_sec1 = toAddress(sec1);
    Address addr_sec2 = toAddress(sec2);
    BOOST_CHECK(addr_sec1 != addr_sec2);
    BOOST_CHECK(addr_pub1 == addr_sec1);
    BOOST_CHECK(addr_pub2 == addr_sec2);
    // check toAddress with nonce
    u256 nonce("13432343243");
    u256 invalid_nonce("9987543");
    Address addr_nonce1 = toAddress(addr_pub2, nonce);
    Address addr_nonce2 = toAddress(addr_sec2, nonce);
    BOOST_CHECK(addr_nonce1 == addr_nonce2);
    // change nonce
    addr_nonce2 = toAddress(addr_pub2, invalid_nonce);
    BOOST_CHECK(addr_nonce1 != addr_nonce2);
    // change address
    addr_nonce2 = toAddress(addr_pub1, nonce);
    BOOST_CHECK(addr_nonce2 != addr_nonce1);
}

/// test encryption and decryption
BOOST_AUTO_TEST_CASE(testEncryptDecrypt)
{
    KeyPair key_pair = KeyPair::create();
    // test encryption
    std::string plain("34lsdafl");
    bytes ciper_ret(plain.begin(), plain.end());
    bytes ret_ciper_ecies(plain.begin(), plain.end());
    dev::encrypt(key_pair.pub(), dev::ref(ciper_ret), ciper_ret);
    // encrypt ecies
    encryptECIES(key_pair.pub(), ref(ret_ciper_ecies), ret_ciper_ecies);
    // std::cout<<"Encryption result:"<<toHex(ciper_ret)<<std::endl;
    // test decryption: normal path
    bool result = decrypt(key_pair.secret(), ref(ciper_ret), ciper_ret);
    BOOST_CHECK(result == true && asString(ref(ciper_ret)) == plain);
    // decrypt ecies
    result = decryptECIES(key_pair.secret(), ref(ret_ciper_ecies), ret_ciper_ecies);
    BOOST_CHECK(result == true && asString(ref(ret_ciper_ecies)) == plain);

    // test decryption: invalid ciper
    std::string invalid_ciper = "23k4jlkfjlskdjfsd";
    bytes invalid_ciper_ret(invalid_ciper.begin(), invalid_ciper.end());
    result = decrypt(key_pair.secret(), ref(invalid_ciper_ret), invalid_ciper_ret);
    BOOST_CHECK(result == false);
    // decryptECIES
    result = decryptECIES(key_pair.secret(), ref(invalid_ciper_ret), invalid_ciper_ret);
    BOOST_CHECK(result == false);
    // test decryption: invalid secret(can't recover plain according ciper)
    Secret invalid_sec = Secret::random();
    while (invalid_sec == key_pair.secret())
        invalid_sec = Secret::random();
    result = decrypt(invalid_sec, ref(ciper_ret), ciper_ret);
    BOOST_CHECK(result == false);
    result = decryptECIES(invalid_sec, ref(ret_ciper_ecies), ret_ciper_ecies);
    BOOST_CHECK(result == false);
}

/// test symmetric encryption and decryption
BOOST_AUTO_TEST_CASE(testSymEncAndDec)
{
    Secret sec = Secret::random();
    // encrypt
    std::string plain = "34dlkwrlkj";
    bytes ret_ciper(plain.begin(), plain.end());
    encryptSym(sec, ref(ret_ciper), ret_ciper);
    // std::cout << "symmetric encryption:" << toHex(ret_ciper) << std::endl;
    // decrypt: normal path
    bytes backup_ciper(ret_ciper.begin(), ret_ciper.end());
    bool result = decryptSym(sec, ref(ret_ciper), ret_ciper);
    BOOST_CHECK(result == true && asString(ref(ret_ciper)) == plain);
    // decrypt: invalid key
    Secret invalid_sec = Secret::random();
    while (invalid_sec == sec)
        invalid_sec = Secret::random();
    result = decryptSym(invalid_sec, ref(backup_ciper), ret_ciper);
    BOOST_CHECK(result == false);
    // decrypt: invalid ciper
    std::string invalid_ciper_str =
        "0403cc4224f03792d72d8c68c3c8e24887"
        "7a62e93b5e16909307e2dbc929df22b3a25fa9cf61c506eb6d62fc66ae19d5a8"
        "85aea8302418748fb146433533e3216e1b7a67a51a10ef2e36a61f1f4eb4773a"
        "d235b7bfd11001a0c009df777987";
    bytes invalid_ciper = fromHex(invalid_ciper_str);
    result = decrypt(sec, ref(invalid_ciper), ret_ciper);
    BOOST_CHECK(result == false);
}

/// test encryptECIES and decryptECIES
BOOST_AUTO_TEST_CASE(testEncAndDecECIESWithMac)
{
    std::string plain_text = "wer3409980";
    bytes ret_ciper(plain_text.begin(), plain_text.end());
    std::string mac_str =
        "17d5df7d4cd9579f7f321c870a44970cce"
        "ecced0ffd16c58fdb88e303c139a19";
    bytes mac(mac_str.begin(), mac_str.end());
    KeyPair key_pair = KeyPair::create();
    // encryptECEIES with mac
    encryptECIES(key_pair.pub(), ref(mac), ref(ret_ciper), ret_ciper);
    // encryptECEIES with mac
    bool result = decryptECIES(key_pair.secret(), ref(mac), ref(ret_ciper), ret_ciper);
    BOOST_CHECK(result == true && asString(ref(ret_ciper)) == plain_text);
    // exception test
    std::string invalid_mac_str =
        "27d5df7d4cd9579f7f321c870a44970cce"
        "ecced0ffd16c58fdb88e303c139a19";
    mac.assign(invalid_mac_str.begin(), invalid_mac_str.end());
    result = decryptECIES(key_pair.secret(), ref(mac), ref(ret_ciper), ret_ciper);
    BOOST_CHECK(result == false);
}

// test encryptSymNoAuth and decryptSymNoAuth
BOOST_AUTO_TEST_CASE(ecies_aes128_ctr)
{
    SecureFixedHash<16> k(sha3("0xAAAA"), h128::AlignLeft);
    std::string m = "AAAAAAAAAAAAAAAA";
    bytesConstRef msg((byte*)m.data(), m.size());

    bytes ciphertext;
    h128 iv;
    tie(ciphertext, iv) = encryptSymNoAuth(k, msg);

    bytes plaintext = decryptSymNoAuth(k, iv, &ciphertext).makeInsecure();
    BOOST_REQUIRE_EQUAL(asString(plaintext), m);
}

/// test ECIES AES128 CTR
BOOST_AUTO_TEST_CASE(testEciesAes128CtrUnaligned)
{
    SecureFixedHash<16> encryptK(sha3("..."), h128::AlignLeft);
    h256 egressMac(sha3("+++"));
    // TESTING: send encrypt magic sequence
    bytes magic{0x22, 0x40, 0x08, 0x91};
    bytes magicCipherAndMac;
    magicCipherAndMac = encryptSymNoAuth(encryptK, h128(), &magic);

    magicCipherAndMac.resize(magicCipherAndMac.size() + 32);
    sha3mac(egressMac.ref(), &magic, egressMac.ref());
    egressMac.ref().copyTo(bytesRef(&magicCipherAndMac).cropped(magicCipherAndMac.size() - 32, 32));

    bytesConstRef cipher(&magicCipherAndMac[0], magicCipherAndMac.size() - 32);
    bytes plaintext = decryptSymNoAuth(encryptK, h128(), cipher).makeInsecure();

    plaintext.resize(magic.size());
    BOOST_REQUIRE(plaintext.size() > 0);
    BOOST_REQUIRE(magic == plaintext);
}

/// test key pair
BOOST_AUTO_TEST_CASE(testEcKeypair)
{
    KeyPair k = KeyPair::create();
    BOOST_CHECK(k.secret());
    BOOST_CHECK(k.pub());
    Public test = toPublic(k.secret());
    BOOST_CHECK_EQUAL(k.pub(), test);

    Secret empty;
    KeyPair kNot(empty);
    BOOST_CHECK(!kNot.address());
    KeyPair k2(sha3(empty));
    BOOST_CHECK(k2.address());
}

BOOST_AUTO_TEST_CASE(testKdf) {}

/// test nonce
BOOST_AUTO_TEST_CASE(testNonce)
{
    BOOST_CHECK(dev::crypto::Nonce::get() != dev::crypto::Nonce::get());
}

/// test ecdha
BOOST_AUTO_TEST_CASE(testEcdh)
{
    auto sec = Secret{sha3("ecdhAgree")};
    auto pub = toPublic(sec);
    Secret sharedSec;
    BOOST_CHECK(dev::crypto::ecdh::agree(sec, pub, sharedSec));
    BOOST_CHECK(sharedSec);
    auto expectedSharedSec = "8ac7e464348b85d9fdfc0a81f2fdc0bbbb8ee5fb3840de6ed60ad9372e718977";
    BOOST_CHECK_EQUAL(sharedSec.makeInsecure().hex(), expectedSharedSec);
}

BOOST_AUTO_TEST_CASE(testSigAndVerify)
{
    KeyPair key_pair = KeyPair::create();
    h256 hash = sha3("abcd");
    /// normal check
    // sign
    Signature sig = sign(key_pair.secret(), hash);
    BOOST_CHECK(SignatureStruct(sig).isValid() == true);
    // verify
    bool result = verify(key_pair.pub(), sig, hash);
    BOOST_CHECK(result == true);
    // recover
    Public pub = recover(sig, hash);
    BOOST_CHECK(pub == key_pair.pub());
    /// exception check:
    // check1: invalid payload(hash)
    h256 invalid_hash = sha3("abce");
    result = verify(key_pair.pub(), sig, invalid_hash);
    BOOST_CHECK(result == false);
    Public invalid_pub = {};
    invalid_pub = recover(sig, invalid_hash);
    BOOST_CHECK(invalid_pub != key_pair.pub());

    // check2: invalid sig
    Signature another_sig(sign(key_pair.secret(), invalid_hash));
    result = verify(key_pair.pub(), another_sig, hash);
    BOOST_CHECK(result == false);

    invalid_pub = recover(another_sig, hash);
    BOOST_CHECK(invalid_pub != key_pair.pub());
    // check3: invalid secret
    Public random_key = Public::random();
    result = verify(random_key, sig, hash);
    BOOST_CHECK(result == false);

    // construct invalid r, v,s and check isValid() function
    h256 r(sha3("+++"));
    h256 s(sha3("24324"));
    byte v = 4;
    SignatureStruct constructed_sig(r, s, v);
    BOOST_CHECK(constructed_sig.isValid() == false);
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace dev
