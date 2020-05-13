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
 * @brief Unit tests for the Transaction
 * @file TransactionTest.cpp
 * @author: chaychen
 * @date 2018
 */
#include <iostream>

#include <libconfig/GlobalConfigure.h>
#include <libdevcore/Assertions.h>
#include <libdevcore/CommonJS.h>
#include <libethcore/CommonJS.h>
#include <libethcore/Transaction.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace dev;
using namespace dev::eth;
namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(TransactionTest, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(testCreateTxByRLP)
{
    u256 value = u256(100);
    u256 gas = u256(100000000);
    u256 gasPrice = u256(0);
    Address dst = toAddress(KeyPair::create().pub());
    std::string str = "test transaction";
    bytes data(str.begin(), str.end());
    Transaction tx(value, gasPrice, gas, dst, data);
    KeyPair sigKeyPair = KeyPair::create();
    std::shared_ptr<crypto::Signature> sig =
        dev::crypto::Sign(sigKeyPair, tx.sha3(WithoutSignature));
    /// update the signature of transaction
    tx.updateSignature(sig);
    /// test encode
    bytes encodeBytes;
    tx.encode(encodeBytes, eth::IncludeSignature::WithSignature);
    /// test decode
    Transaction decodeTx;
    decodeTx.decode(ref(encodeBytes));
    BOOST_CHECK(tx == decodeTx);
    BOOST_CHECK(decodeTx.sender() == tx.safeSender());
    BOOST_CHECK_NO_THROW(decodeTx.signature());
    /*bytes s;
    BOOST_CHECK_NO_THROW(tx.encode(s, eth::IncludeSignature::WithSignature));*/

    auto version = g_BCOSConfig.version();
    auto supportedVersion = g_BCOSConfig.supportedVersion();
    // RC1_VERSION encode and decode
    g_BCOSConfig.setSupportedVersion("2.0.0-rc1", RC1_VERSION);
    /// test encode
    bytes encodeBytesRC1;
    BOOST_CHECK_NO_THROW(tx.encode(encodeBytesRC1, eth::IncludeSignature::WithSignature));
    /// test decode
    bytes rlpBytesRC1 = fromHex(
        "f8ef9f65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d03f85174876e7ff"
        "8609184e729fff82020394d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce000000"
        "000000000000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292"
        "ff4aaa5797bf671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7"
        "f7395028658d0e01b86a371ca00b2b3fabd8598fefdda4efdb54f626367fc68e1735a8047f0f1c4f84"
        "0255ca1ea0512500bc29f4cfe18ee1c88683006d73e56c934100b8abf4d2334560e1d2f75e");
    Transaction decodeTxRC1;
    BOOST_CHECK_NO_THROW(decodeTxRC1.decode(ref(rlpBytesRC1)));
    g_BCOSConfig.setSupportedVersion(supportedVersion, version);
}

BOOST_FIXTURE_TEST_CASE(SM_testCreateTxByRLP, SM_CryptoTestFixture)
{
    u256 value = u256(100);
    u256 gas = u256(100000000);
    u256 gasPrice = u256(0);
    Address dst = toAddress(KeyPair::create().pub());
    std::string str = "test transaction";
    bytes data(str.begin(), str.end());
    Transaction tx(value, gasPrice, gas, dst, data);
    KeyPair sigKeyPair = KeyPair::create();
    std::shared_ptr<crypto::Signature> sig =
        dev::crypto::Sign(sigKeyPair, tx.sha3(WithoutSignature));
    /// update the signature of transaction
    tx.updateSignature(sig);
    /// test encode
    bytes encodeBytes;
    tx.encode(encodeBytes, eth::IncludeSignature::WithSignature);
    /// test decode
    Transaction decodeTx;
    decodeTx.decode(ref(encodeBytes));
    BOOST_CHECK(tx == decodeTx);
    BOOST_CHECK(decodeTx.sender() == tx.safeSender());
    BOOST_CHECK_NO_THROW(decodeTx.signature());
    /*bytes s;
    BOOST_CHECK_NO_THROW(tx.encode(s, eth::IncludeSignature::WithSignature));*/

    auto version = g_BCOSConfig.version();
    auto supportedVersion = g_BCOSConfig.supportedVersion();
    // RC1_VERSION encode and decode
    g_BCOSConfig.setSupportedVersion("2.0.0-rc1", RC1_VERSION);
    /// test encode
    bytes encodeBytesRC1;
    BOOST_CHECK_NO_THROW(tx.encode(encodeBytesRC1, eth::IncludeSignature::WithSignature));
    /// test decode
    bytes rlpBytesRC1 = fromHex(
        "f901309f65f0d06e39dc3c08e32ac10a5070858962bc6c0f5760baca823f2d5582d14485174876e7ff8609"
        "184e729fff8204a294d6f1a71052366dbae2f7ab2d5d5845e77965cf0d80b86448f85bce00000000000000"
        "0000000000000000000000000000000000000000000000001bf5bd8a9e7ba8b936ea704292ff4aaa5797bf"
        "671fdc8526dcd159f23c1f5a05f44e9fa862834dc7cb4541558f2b4961dc39eaaf0af7f7395028658d0e01"
        "b86a37b840c7ca78e7ab80ee4be6d3936ba8e899d8fe12c12114502956ebe8c8629d36d88481dec9973574"
        "2ea523c88cf3becba1cc4375bc9e225143fe1e8e43abc8a7c493a0ba3ce8383b7c91528bede9cf890b4b1e"
        "9b99c1d8e56d6f8292c827470a606827a0ed511490a1666791b2bd7fc4f499eb5ff18fb97ba68ff9aee206"
        "8fd63b88e817");
    Transaction decodeTxRC1;
    BOOST_CHECK_NO_THROW(decodeTxRC1.decode(ref(rlpBytesRC1)));
    g_BCOSConfig.setSupportedVersion(supportedVersion, version);

    version = g_BCOSConfig.version();
    supportedVersion = g_BCOSConfig.supportedVersion();
    g_BCOSConfig.setSupportedVersion("2.0.0-rc2", RC2_VERSION);

    Transaction decodeTxRC2;
    bytes rlpBytesRC2 = fromHex(
        "f904a8a00148abbf95a2cd7851a238a393cd0b37e98c6b7602d86765633dc58ca5a27d538411e1a3008411e1a3"
        "00830314f08080b903ed608060405234801561001057600080fd5b506040805190810160405280600d81526020"
        "017f48656c6c6f2c20576f726c6421000000000000000000000000000000000000008152506000908051906020"
        "019061005c929190610062565b50610107565b8280546001816001161561010002031660029004906000526020"
        "60002090601f016020900481019282601f106100a357805160ff19168380011785556100d1565b828001600101"
        "855582156100d1579182015b828111156100d05782518255916020019190600101906100b5565b5b5090506100"
        "de91906100e2565b5090565b61010491905b808211156101005760008160009055506001016100e8565b509056"
        "5b90565b6102d7806101166000396000f30060806040526004361061004c576000357c01000000000000000000"
        "00000000000000000000000000000000000000900463ffffffff168063299f7f9d146100515780633590b49f14"
        "6100e1575b600080fd5b34801561005d57600080fd5b5061006661014a565b6040518080602001828103825283"
        "818151815260200191508051906020019080838360005b838110156100a6578082015181840152602081019050"
        "61008b565b50505050905090810190601f1680156100d35780820380516001836020036101000a031916815260"
        "200191505b509250505060405180910390f35b3480156100ed57600080fd5b5061014860048036038101908080"
        "3590602001908201803590602001908080601f0160208091040260200160405190810160405280939291908181"
        "5260200183838082843782019150505050505091929192905050506101ec565b005b6060600080546001816001"
        "16156101000203166002900480601f016020809104026020016040519081016040528092919081815260200182"
        "8054600181600116156101000203166002900480156101e25780601f106101b757610100808354040283529160"
        "2001916101e2565b820191906000526020600020905b8154815290600101906020018083116101c55782900360"
        "1f168201915b5050505050905090565b8060009080519060200190610202929190610206565b5050565b828054"
        "600181600116156101000203166002900490600052602060002090601f016020900481019282601f1061024757"
        "805160ff1916838001178555610275565b82800160010185558215610275579182015b82811115610274578251"
        "825591602001919060010190610259565b5b5090506102829190610286565b5090565b6102a891905b80821115"
        "6102a457600081600090555060010161028c565b5090565b905600a165627a7a723058200e531208a19ed0dffe"
        "ce0f39430f0289ccaf453693b7f502c0d96ddafd58e9840029010180b8400049d746344c4b535ed9462ac52c12"
        "d059e94946a663a4aa91d02af8df0e8e8336a2d32e1ee7745ca2769ea70e5f86cb2ec315f77bbf8de3a90c32ce"
        "2125ea8fa035fcfcd60f0ee3b088b673e70334bb56fbed5edc57555393621edb8c0ccdbee2a0075b746d47379a"
        "6a32534f792e75eb0f86d20ef77240f0b4ca78747e4e81b53f");
    BOOST_CHECK_NO_THROW(decodeTxRC2.decode(ref(rlpBytesRC2)));
    g_BCOSConfig.setSupportedVersion(supportedVersion, version);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
