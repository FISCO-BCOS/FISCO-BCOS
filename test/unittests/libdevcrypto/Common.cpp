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
 * @brief unit test for Common.* of devcrypto module
 *
 * @file Common.cpp
 * @author yujiechen
 * @date 2018-08-28
 */
#include "libdevcrypto/Hash.h"
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
#ifdef FISCO_GM
BOOST_AUTO_TEST_CASE(GM_testCommonTrans)
{
    BOOST_CHECK(Secret::size == 32);
    BOOST_CHECK(Public::size == 64);
    BOOST_CHECK(Signature::size == 128);
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

/// test key pair
BOOST_AUTO_TEST_CASE(GM_testEcKeypair)
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

BOOST_AUTO_TEST_CASE(GM_testKdf) {}

/// test nonce
BOOST_AUTO_TEST_CASE(GM_testNonce)
{
    BOOST_CHECK(dev::crypto::Nonce::get() != dev::crypto::Nonce::get());
}

// /// test ecdha
BOOST_AUTO_TEST_CASE(GM_testEcdh)
{
    auto sec = Secret{sha3("ecdhAgree")};
    Secret sharedSec;
    auto expectedSharedSec = "0000000000000000000000000000000000000000000000000000000000000000";
    BOOST_CHECK_EQUAL(sharedSec.makeInsecure().hex(), expectedSharedSec);
}

BOOST_AUTO_TEST_CASE(GM_testSigAndVerify)
{
    KeyPair key_pair = KeyPair::create();
    h256 hash = sha3("abcd");
    /// normal check
    // sign
    Signature sig = sign(key_pair, hash);
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
    Signature another_sig(sign(key_pair, invalid_hash));
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
    h512 v(sha3("123456"));
    SignatureStruct constructed_sig(r, s, v);
    BOOST_CHECK(constructed_sig.isValid() == true);
}
/// test ecRocer
BOOST_AUTO_TEST_CASE(GM_testSigecRocer)
{
    std::pair<bool, bytes> KeyPair;
#ifdef FISCO_GM
    bytes rlpBytes = fromHex(
        "f901309f65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d14485174876e7ff8609"
        "184e729fff8204a294d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce00000000000000"
        "0000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292ff4aaa5797bf"
        "671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7f7395028658d0e01"
        "b86a37b840c7ca78e7ab80ee4be6d3936ba8e899d8fe12c12114502956ebe8c8629d36d88481dec9973574"
        "2ea523c88cf3becba1cc4375bc9e225143fe1e8e43abc8a7c493a0ba3ce8383b7c91528bede9cf890b4b1e"
        "9b99c1d8e56d6f8292c827470a606827a0ed511490a1666791b2bd7fc4f499eb5ff18fb97ba68ff9aee206"
        "8fd63b88e817");
#else
    bytes rlpBytes = fromHex(
        "f8ef9f65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f85174876e7ff"
        "8609184e729fff82020394d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce000000"
        "000000000000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292"
        "ff4aaa5797bf671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7"
        "f7395028658d0e01b86a371ca00b2b3fabd8598fefdda4efdb54f626367fc68e1735a8047f0f1c4f84"
        "0255ca1ea0512500bc29f4cfe18ee1c88683006d73e56c934100b8abf4d2334560e1d2f75e");
#endif
    h256 ret;
    RLP rlpObj(rlpBytes);
    bytesConstRef _in = rlpObj.data();
    KeyPair = SignatureStruct::ecRecover(_in);
    BOOST_CHECK(KeyPair.first == true);
    BOOST_CHECK(KeyPair.second != ret.asBytes());
}
#else
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

BOOST_AUTO_TEST_CASE(testSigAndVerify)
{
    KeyPair key_pair = KeyPair::create();
    h256 hash = sha3("abcd");
    /// normal check
    // sign
    Signature sig = sign(key_pair, hash);
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
    Signature another_sig(sign(key_pair, invalid_hash));
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
    SignatureStruct constructed_sig(r, s, v - 27);
    BOOST_CHECK(constructed_sig.isValid() == false);
}
/// test ecRocer
BOOST_AUTO_TEST_CASE(testSigecRocer)
{
    std::pair<bool, bytes> KeyPair;
    std::pair<bool, bytes> KeyPairR;
    bytes rlpBytes = fromHex(
        "f8ef9f65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f85174876e7ff"
        "8609184e729fff82020394d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce000000"
        "000000000000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292"
        "ff4aaa5797bf671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7"
        "f7395028658d0e01b86a371ca00b2b3fabd8598fefdda4efdb54f626367fc68e1735a8047f0f1c4f84"
        "0255ca1ea0512500bc29f4cfe18ee1c88683006d73e56c934100b8abf4d2334560e1d2f75e");

    bytes rlpBytesRight = fromHex(
        "38d18acb67d25c8bb9942764b62f18e17054f66a817bd4295423adf9ed98873e"
        "000000000000000000000000000000000000000000000000000000000000001b"
        "38d18acb67d25c8bb9942764b62f18e17054f66a817bd4295423adf9ed98873e"
        "789d1dd423d25f0772d2748d60f7e4b81bb14d086eba8e8e8efb6dcff8a4ae02");

    h256 ret("000000000000000000000000ceaccac640adf55b2028469bd36ba501f28b699d");
    RLP rlpObj(rlpBytes);
    bytesConstRef _in = rlpObj.data();
    KeyPair = SignatureStruct::ecRecover(_in);
    KeyPairR = SignatureStruct::ecRecover(ref(rlpBytesRight));
    BOOST_CHECK(KeyPair.first == true);
    BOOST_CHECK(KeyPair.second != ret.asBytes());
    BOOST_CHECK(KeyPairR.second == ret.asBytes());
}
#endif


BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace dev
