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
#include "libdevcrypto/CryptoInterface.h"
#include "libdevcrypto/ECDSASignature.h"
#include "libdevcrypto/SM2Signature.h"
#include <libdevcrypto/Common.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace std;
using namespace bcos;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(SM_DevcryptoCommonTest, SM_CryptoTestFixture)
BOOST_AUTO_TEST_CASE(SM_testCommonTrans)
{
    BOOST_CHECK(Secret::size == 32);
    BOOST_CHECK(Public::size == 64);
    // check secret->public
    Secret sec1(
        "bcec428d5205abe0f0cc8a7340839"
        "08d9eb8563e31f943d760786edf42ad67dd");
    Public pub1 = toPublic(sec1);
    Secret sec2("bcec428d5205abe0");
    Public pub2 = toPublic(sec2);
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
BOOST_AUTO_TEST_CASE(SM_testEcKeypair)
{
    KeyPair k = KeyPair::create();
    BOOST_CHECK(k.secret());
    BOOST_CHECK(k.pub());
    Public test = toPublic(k.secret());
    BOOST_CHECK_EQUAL(k.pub(), test);

    Secret empty;
    KeyPair kNot(empty);
    BOOST_CHECK(!kNot.address());
#if 0
    KeyPair k2(crypto::Hash(empty));
    BOOST_CHECK(k2.address());
#endif
}

BOOST_AUTO_TEST_CASE(SM_testKdf) {}

/// test nonce
BOOST_AUTO_TEST_CASE(SM_testNonce)
{
    BOOST_CHECK(bcos::crypto::Nonce::get() != bcos::crypto::Nonce::get());
}

// /// test ecdha
BOOST_AUTO_TEST_CASE(SM_testEcdh)
{
    auto sec = Secret{crypto::Hash("ecdhAgree")};
    Secret sharedSec;
    auto expectedSharedSec = "0000000000000000000000000000000000000000000000000000000000000000";
    BOOST_CHECK_EQUAL(sharedSec.makeInsecure().hex(), expectedSharedSec);
}

BOOST_AUTO_TEST_CASE(SM_testSigAndVerify)
{
    KeyPair key_pair = KeyPair::create();
    h256 hash = crypto::Hash("abcd");
    /// normal check
    // sign
    auto sig = crypto::Sign(key_pair, hash);
    BOOST_CHECK(sig->isValid() == true);
    // verify
    bool result = crypto::Verify(key_pair.pub(), sig, hash);
    BOOST_CHECK(result == true);
    // recover
    Public pub = crypto::Recover(sig, hash);
    BOOST_CHECK(pub == key_pair.pub());
    /// exception check:
    // check1: invalid payload(hash)
    h256 invalid_hash = crypto::Hash("abce");
    result = crypto::Verify(key_pair.pub(), sig, invalid_hash);
    BOOST_CHECK(result == false);
    Public invalid_pub = {};
    invalid_pub = crypto::Recover(sig, invalid_hash);
    BOOST_CHECK(invalid_pub != key_pair.pub());

    // check2: invalid sig
    auto another_sig(crypto::Sign(key_pair, invalid_hash));
    result = crypto::Verify(key_pair.pub(), another_sig, hash);
    BOOST_CHECK(result == false);

    invalid_pub = crypto::Recover(another_sig, hash);
    BOOST_CHECK(invalid_pub != key_pair.pub());
    // check3: invalid secret
    Public random_key = Public::generateRandomFixedBytes();
    result = crypto::Verify(random_key, sig, hash);
    BOOST_CHECK(result == false);

    // construct invalid r, v,s and check isValid() function
    h256 r(crypto::Hash("+++"));
    h256 s(crypto::Hash("24324"));
    h512 v(crypto::Hash("123456"));
    auto constructed_sig = std::make_shared<crypto::SM2Signature>(r, s, v);
    BOOST_CHECK(constructed_sig->isValid() == true);
}
/// test ecRocer
BOOST_AUTO_TEST_CASE(SM_testSigecRocer)
{
    std::pair<bool, bytes> KeyPair;
    bytes rlpBytes = *fromHexString(
        "f901309f65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d14485174876e7ff8609"
        "184e729fff8204a294d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce00000000000000"
        "0000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292ff4aaa5797bf"
        "671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7f7395028658d0e01"
        "b86a37b840c7ca78e7ab80ee4be6d3936ba8e899d8fe12c12114502956ebe8c8629d36d88481dec9973574"
        "2ea523c88cf3becba1cc4375bc9e225143fe1e8e43abc8a7c493a0ba3ce8383b7c91528bede9cf890b4b1e"
        "9b99c1d8e56d6f8292c827470a606827a0ed511490a1666791b2bd7fc4f499eb5ff18fb97ba68ff9aee206"
        "8fd63b88e817");
    h256 ret;
    RLP rlpObj(rlpBytes);
    bytesConstRef _in = rlpObj.data();
    KeyPair = recover(_in);
    BOOST_CHECK(KeyPair.first == true);
    BOOST_CHECK(KeyPair.second != ret.asBytes());
}
BOOST_AUTO_TEST_SUITE_END()

BOOST_FIXTURE_TEST_SUITE(DevcryptoCommonTest, TestOutputHelperFixture)
/// test toPublic && toAddress
BOOST_AUTO_TEST_CASE(testCommonTrans)
{
    BOOST_CHECK(Secret::size == 32);
    BOOST_CHECK(Public::size == 64);
    // check secret->public
    Secret sec1(
        "bcec428d5205abe0f0cc8a7340839"
        "08d9eb8563e31f943d760786edf42ad67dd");
    Public pub1 = toPublic(sec1);
    Secret sec2("bcec428d5205abe0");
    Public pub2 = toPublic(sec2);
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
}

BOOST_AUTO_TEST_CASE(testKdf) {}

/// test nonce
BOOST_AUTO_TEST_CASE(testNonce)
{
    BOOST_CHECK(bcos::crypto::Nonce::get() != bcos::crypto::Nonce::get());
}

BOOST_AUTO_TEST_CASE(testSigAndVerify)
{
    KeyPair key_pair = KeyPair::create();
    h256 hash = crypto::Hash("abcd");
    /// normal check
    // sign
    auto sig = crypto::Sign(key_pair, hash);
    BOOST_CHECK(sig->isValid() == true);
    // verify
    bool result = crypto::Verify(key_pair.pub(), sig, hash);
    BOOST_CHECK(result == true);
    // recover
    Public pub = crypto::Recover(sig, hash);
    BOOST_CHECK(pub == key_pair.pub());
    /// exception check:
    // check1: invalid payload(hash)
    h256 invalid_hash = crypto::Hash("abce");
    result = crypto::Verify(key_pair.pub(), sig, invalid_hash);
    BOOST_CHECK(result == false);
    Public invalid_pub = {};
    invalid_pub = crypto::Recover(sig, invalid_hash);
    BOOST_CHECK(invalid_pub != key_pair.pub());

    // check2: invalid sig
    auto another_sig(crypto::Sign(key_pair, invalid_hash));
    result = crypto::Verify(key_pair.pub(), another_sig, hash);
    BOOST_CHECK(result == false);

    invalid_pub = crypto::Recover(another_sig, hash);
    BOOST_CHECK(invalid_pub != key_pair.pub());
    // check3: invalid secret
    Public random_key = Public::generateRandomFixedBytes();
    result = crypto::Verify(random_key, sig, hash);
    BOOST_CHECK(result == false);

    // construct invalid r, v,s and check isValid() function
    h256 r(crypto::Hash("+++"));
    h256 s(crypto::Hash("24324"));
    byte v = 4;
    auto constructed_sig = std::make_shared<crypto::ECDSASignature>(r, s, v - 27);
    BOOST_CHECK(constructed_sig->isValid() == false);
}
/// test ecRocer
BOOST_AUTO_TEST_CASE(testSigecRocer)
{
    std::pair<bool, bytes> keyPair;
    std::pair<bool, bytes> KeyPairR;
    bytes rlpBytes = *fromHexString(
        "f8ef9f65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f85174876e7ff"
        "8609184e729fff82020394d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce000000"
        "000000000000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292"
        "ff4aaa5797bf671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7"
        "f7395028658d0e01b86a371ca00b2b3fabd8598fefdda4efdb54f626367fc68e1735a8047f0f1c4f84"
        "0255ca1ea0512500bc29f4cfe18ee1c88683006d73e56c934100b8abf4d2334560e1d2f75e");

    bytes rlpBytesRight = *fromHexString(
        "38d18acb67d25c8bb9942764b62f18e17054f66a817bd4295423adf9ed98873e"
        "000000000000000000000000000000000000000000000000000000000000001b"
        "38d18acb67d25c8bb9942764b62f18e17054f66a817bd4295423adf9ed98873e"
        "789d1dd423d25f0772d2748d60f7e4b81bb14d086eba8e8e8efb6dcff8a4ae02");

    h256 ret("000000000000000000000000ceaccac640adf55b2028469bd36ba501f28b699d");
    RLP rlpObj(rlpBytes);
    bytesConstRef _in = rlpObj.data();
    keyPair = bcos::ecRecover(_in);
    BOOST_CHECK(keyPair.first == true);
    BOOST_CHECK(keyPair.second != ret.asBytes());
    KeyPairR = bcos::ecRecover(ref(rlpBytesRight));
    cout << *toHexString(KeyPairR.second) << endl;
    cout << *toHexString(ret.asBytes()) << endl;
    BOOST_CHECK(KeyPairR.second == ret.asBytes());
}
BOOST_AUTO_TEST_CASE(testSign)
{
    std::string publicKey =
        "15a264e29489f69fb74608a8a26600eb8f5c572d5829531aaab3246f7411492e2c4d3c329b6f4e5fc479908a29"
        "44688da39071467035f8eb046625259a6bfd06";
    std::string signature =
        "0xb2ef97867ca3b1030c2f14be1bfeeeabddf3354ae6e57c86a02ebc1383f8ef6c7713d7f12fcfd5da457f58ee"
        "eb9aa77eddedf69b7bd1a46adb5faf072e62892601";
    std::string blockHash = "0x39aeeacf66784ba18836280dcb56e454fe59eecde42812503e6a0d2c0a11937f";
    bcos::h512 publicKeyBytes = bcos::h512(publicKey);
    bytes signatureBytes = *fromHexString(signature);
    bcos::h256 blockHashBytes = bcos::h256(blockHash);
    std::cout << "### before test sign" << std::endl;
    BOOST_CHECK(bcos::ecdsaVerify(publicKeyBytes, bcos::crypto::SignatureFromBytes(signatureBytes),
                    blockHashBytes) == true);
    std::cout << "### test sign passed" << std::endl;
}


BOOST_AUTO_TEST_SUITE_END()

}  // namespace test
}  // namespace bcos
