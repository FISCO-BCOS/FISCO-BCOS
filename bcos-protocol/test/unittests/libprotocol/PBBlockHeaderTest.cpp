/*
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @brief test for BlockHeader
 * @file PBBlockHeaderTest.h
 * @author: yujiechen
 * @date: 2021-03-16
 */
#include "bcos-protocol/testutils/protocol/FakeBlockHeader.h"
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/hash/SM3.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-crypto/signature/sm2/SM2Crypto.h>
using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::crypto;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(PBBlockHeaderTest, TestPromptFixture)
BOOST_AUTO_TEST_CASE(testNormalPBBlockHeader)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);
    testPBBlockHeader(cryptoSuite);
}

BOOST_AUTO_TEST_CASE(testSMPBBlockHeader)
{
    auto hashImpl = std::make_shared<SM3>();
    auto signImpl = std::make_shared<SM2Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);
    testPBBlockHeader(cryptoSuite);
}

BOOST_AUTO_TEST_CASE(testRawPBBlockHeader)
{
    auto hashImpl = std::make_shared<Keccak256>();
    auto signImpl = std::make_shared<Secp256k1Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);
// FIXME: correct this test when the blockHeader is fixed
#if 0
    auto data = fromHexString(
        "080a12a9040c000000000000000080044852b2a670ade5407e78fb2863c51de9fcb96542a07186fe3aeda6bb8a"
        "116d010000000000000080c89efdaa54c0f20c7adf612882df0950f5a951637e0307cdcb4c672f298b8bc60200"
        "00000000000080ad7c5bef027816a800da1736444fb58a807ef4c9603b7848673f7e3a68eb14a5808fc1d10c13"
        "0a57800c446a858cf3bb4699c25d17f6318484814f18a9e1227f1b80fbc6d2ca221fb45933acae4194e9c1f132"
        "bd6565e678e3a21d088b2545d11c24808ea407c24ec9663ee0e7a5f2d0e9afffec28087c25f296e68585bb292e"
        "d6c5e55cfadedd020000000ba6ed091224037a3b6726020000006400000000000000100101d7fdd8c32486b984"
        "304b4c254b48255c564030a83b89b7e7b9ad2358dedc48afd8eab69f90a3e305d2325fc6034bd9b0d8d004872e"
        "10c98078279d322c56c7ce0101a886ee251bd5799d9453e050413a16cdbc72a7a1f755a018f623843f435de2cc"
        "1ca49d6a0037e5f0aa0253572d70e0218af07043facfbc5d1cb5bf7091c64dd10101a1039da8932ec26b2be14b"
        "fdd34ddb1fbf45a0adb10bfd2a25bc0d4204cf1f101eaf3ec90c30e9535e0ab89227ceccb5804726515f981804"
        "556f4187e068ecb90101f7f2e5c1747316c57d77b8fa8ee53d5b03e04e001c7e9b5b961b1eacfc0bdba94e7985"
        "095d2b25305c47a4e2d6fe615cc7b28955cb5d39525cb00c9e5f9f0d5800808ea407c24ec9663ee0e7a5f2d0e9"
        "afffec28087c25f296e68585bb292ed6c5e51a431241c1564b743653c3ee51155892fb1d53551ae065e3726473"
        "ed52391bcc7f6dc96719518537cc90891243b3796bba578e5f33e76f115b6df91d4e588290dc79f942001a4508"
        "011241141a3a0ec110bfa18c5d8983526fcb5ce185fe846a44708308fe2355ce6bebd94d9a57438895381cd391"
        "5e124dd75c19f25188d22354b133a92df2ed88f802d3001a4508021241798169fce3f226cfc7016e11bb3a06c7"
        "3dbc3721eb59a39fadb4f46e31e804fe3fa5bbe2093925bb95219d03af7ee89553f3e0d172d305737c219f5a5d"
        "5178e3001a4508031241a34453eca746d38c348a0e9aab589eda5dcb6a5ee9759941a35778fe0cc40ee7769abb"
        "947355cbf3f9aaafaa201c481d2712be605e604483ca41d09210e85ff901");
    auto decodedBlockHeader = std::make_shared<PBBlockHeader>(cryptoSuite, *data);

    BOOST_CHECK(decodedBlockHeader->version() == 10);
    BOOST_CHECK(decodedBlockHeader->txsRoot().hex() ==
                "8fc1d10c130a57800c446a858cf3bb4699c25d17f6318484814f18a9e1227f1b");
    BOOST_CHECK(decodedBlockHeader->receiptsRoot().hex() ==
                "fbc6d2ca221fb45933acae4194e9c1f132bd6565e678e3a21d088b2545d11c24");
    BOOST_CHECK(decodedBlockHeader->stateRoot().hex() ==
                "8ea407c24ec9663ee0e7a5f2d0e9afffec28087c25f296e68585bb292ed6c5e5");
    BOOST_CHECK(decodedBlockHeader->number() == 12312312412);
    BOOST_CHECK(decodedBlockHeader->gasUsed() == 3453456346534);
    BOOST_CHECK(decodedBlockHeader->timestamp() == 9234234234);
    BOOST_CHECK(decodedBlockHeader->sealer() == 100);
    BOOST_CHECK(*toHexString(decodedBlockHeader->extraData()) ==
                "8ea407c24ec9663ee0e7a5f2d0e9afffec28087c25f296e68585bb292ed6c5e5");
    // check signature
    BOOST_CHECK(decodedBlockHeader->signatureList().size() == 4);
    BOOST_CHECK(decodedBlockHeader->hash().hex() ==
                "da717c040800a6dd6eaf93380f922e21575db5c55898935297691292576ac893");
    BOOST_CHECK(decodedBlockHeader->parentInfo()[0].blockHash.hex() ==
                "044852b2a670ade5407e78fb2863c51de9fcb96542a07186fe3aeda6bb8a116d");
    BOOST_CHECK(*toHexString(decodedBlockHeader->sealerList()[0]) ==
                "d7fdd8c32486b984304b4c254b48255c564030a83b89b7e7b9ad2358dedc48afd8eab69f90a3e305d2"
                "325fc6034bd9b0d8d004872e10c98078279d322c56c7ce");
    BOOST_CHECK(*toHexString(decodedBlockHeader->sealerList()[1]) ==
                "a886ee251bd5799d9453e050413a16cdbc72a7a1f755a018f623843f435de2cc1ca49d6a0037e5f0aa"
                "0253572d70e0218af07043facfbc5d1cb5bf7091c64dd1");
    BOOST_CHECK(*toHexString(decodedBlockHeader->sealerList()[2]) ==
                "a1039da8932ec26b2be14bfdd34ddb1fbf45a0adb10bfd2a25bc0d4204cf1f101eaf3ec90c30e9535e"
                "0ab89227ceccb5804726515f981804556f4187e068ecb9");
    BOOST_CHECK(*toHexString(decodedBlockHeader->sealerList()[3]) ==
                "f7f2e5c1747316c57d77b8fa8ee53d5b03e04e001c7e9b5b961b1eacfc0bdba94e7985095d2b25305c"
                "47a4e2d6fe615cc7b28955cb5d39525cb00c9e5f9f0d58");

    BOOST_CHECK(*toHexString(decodedBlockHeader->signatureList()[0].signature) ==
                "c1564b743653c3ee51155892fb1d53551ae065e3726473ed52391bcc7f6dc96719518537cc90891243"
                "b3796bba578e5f33e76f115b6df91d4e588290dc79f94200");
    BOOST_CHECK(*toHexString(decodedBlockHeader->signatureList()[1].signature) ==
                "141a3a0ec110bfa18c5d8983526fcb5ce185fe846a44708308fe2355ce6bebd94d9a57438895381cd3"
                "915e124dd75c19f25188d22354b133a92df2ed88f802d300");
    BOOST_CHECK(*toHexString(decodedBlockHeader->signatureList()[2].signature) ==
                "798169fce3f226cfc7016e11bb3a06c73dbc3721eb59a39fadb4f46e31e804fe3fa5bbe2093925bb95"
                "219d03af7ee89553f3e0d172d305737c219f5a5d5178e300");
    BOOST_CHECK(*toHexString(decodedBlockHeader->signatureList()[3].signature) ==
                "a34453eca746d38c348a0e9aab589eda5dcb6a5ee9759941a35778fe0cc40ee7769abb947355cbf3f9"
                "aaafaa201c481d2712be605e604483ca41d09210e85ff901");
    BOOST_CHECK(decodedBlockHeader->consensusWeights().size() == 0);
#endif
}
BOOST_AUTO_TEST_CASE(testRawSMPBBlockHeader)
{
    auto hashImpl = std::make_shared<SM3>();
    auto signImpl = std::make_shared<SM2Crypto>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, signImpl, nullptr);
    // FIXME: correct this test when the blockHeader is fixed
#if 0
    auto data = fromHexString(
        "080a12a9040c00000000000000008006d47b6f2f121e85160d1d8072e58843de1eed164ed526c3b56b22c2b473"
        "24a0010000000000000080cbdddb8e8421b23498480570d7d75330538a6882f5dfdc3b64115c647f3328c40200"
        "00000000000080a0dc2d74b9b0e3c87e076003dbfe472a424cb3032463cb339e351460765a822e80bcd22fd79b"
        "459e46e3fbe2d1c63eb0421ef14479c5f187fa1230459e4052215d8036433bdcdf6144fa6abee2ffccf57897f6"
        "33a1f45e0f82e7796f25fed1b2f94b800f000c0d3cf849632ed9aa2925251c0a13ee4f09f991b30bf396c15bf7"
        "ae80975cfadedd020000000ba6ed091224037a3b6726020000006400000000000000100101f604d70d59bf3cf7"
        "66a6cba8a3fca8f5837d20526062cc06d0bfe2d2624fedadd483518bc73eff22bed25aa633b2cee4f2c7d7b90f"
        "efbf59a27806e403ef5aff01014a676e2641875b76c58c5bce3a8cbb6a1934c9267b51d982add373c38eb3c34a"
        "f333ebad785b5b0cb7df15e7358e764706705d9d2de6c5efe7c13001842273c20101fa4b61c4b5d50bfe270f3f"
        "c5d486a4e262f87dcea20157204374a026fe94c2f17d4e6248fd93c899385c0bce8029fcf1e2a8b8aa1faf083f"
        "2ae88f4d971bdd190101deeb46a1b389fa1e4b225df5c9ced5cffb4cb8c7ad2729eb78b5d99e59b4c64eaa654e"
        "7e955c612998954f5463f25a8e7b6e26a61a100e0ac68b629fa415928200800f000c0d3cf849632ed9aa292525"
        "1c0a13ee4f09f991b30bf396c15bf7ae80971a4212409529fc4f0064656104f209706d4d20d86637c30c2ec35e"
        "d9248695ee453b06754251494fb0ff8f2e9f779c884dbf17d9d97c4b126fb5c7c39c7b6adb44f8a79a1a440801"
        "1240f5af51722e4519709f430d20ea3cf1158dffd30fae01b7548d7c52733217587f31948d405d0792e9b4b852"
        "757fcd92e876f645818dd493cce3fb6310a073dcbd1a44080212401483270d9bd8bc98121035d537d84c8ee7a2"
        "52b5cbd26004cfe8a35b31461df8b90de93453ee713cac973a559039905a8009613d06941eab935bc017c8fdc1"
        "d21a4408031240d058eca499d70c0c751672f407c263488b28194ea970fe8ed09d03945ae84e75ec2fb395370d"
        "6486552cef01208a4436f9f790ffa16f4d0c0538b08a29dd9c58");
    auto decodedBlockHeader = std::make_shared<PBBlockHeader>(cryptoSuite, *data);
    BOOST_CHECK(decodedBlockHeader->version() == 10);
    BOOST_CHECK(decodedBlockHeader->txsRoot().hex() ==
                "bcd22fd79b459e46e3fbe2d1c63eb0421ef14479c5f187fa1230459e4052215d");
    BOOST_CHECK(decodedBlockHeader->receiptsRoot().hex() ==
                "36433bdcdf6144fa6abee2ffccf57897f633a1f45e0f82e7796f25fed1b2f94b");
    BOOST_CHECK(decodedBlockHeader->stateRoot().hex() ==
                "0f000c0d3cf849632ed9aa2925251c0a13ee4f09f991b30bf396c15bf7ae8097");
    BOOST_CHECK(decodedBlockHeader->number() == 12312312412);
    BOOST_CHECK(decodedBlockHeader->gasUsed() == 3453456346534);
    BOOST_CHECK(decodedBlockHeader->timestamp() == 9234234234);
    BOOST_CHECK(decodedBlockHeader->sealer() == 100);
    BOOST_CHECK(*toHexString(decodedBlockHeader->extraData()) ==
                "0f000c0d3cf849632ed9aa2925251c0a13ee4f09f991b30bf396c15bf7ae8097");
    // check signature
    BOOST_CHECK(decodedBlockHeader->signatureList().size() == 4);
    BOOST_CHECK(decodedBlockHeader->hash().hex() ==
                "ce67da303b36a5ee46736fa75e1b47a65089637dd8294911dab54d57d4d4a7b1");
    BOOST_CHECK(decodedBlockHeader->parentInfo()[0].blockHash.hex() ==
                "06d47b6f2f121e85160d1d8072e58843de1eed164ed526c3b56b22c2b47324a0");
    BOOST_CHECK(*toHexString(decodedBlockHeader->sealerList()[0]) ==
                "f604d70d59bf3cf766a6cba8a3fca8f5837d20526062cc06d0bfe2d2624fedadd483518bc73eff22be"
                "d25aa633b2cee4f2c7d7b90fefbf59a27806e403ef5aff");
    BOOST_CHECK(*toHexString(decodedBlockHeader->sealerList()[1]) ==
                "4a676e2641875b76c58c5bce3a8cbb6a1934c9267b51d982add373c38eb3c34af333ebad785b5b0cb7"
                "df15e7358e764706705d9d2de6c5efe7c13001842273c2");
    BOOST_CHECK(*toHexString(decodedBlockHeader->sealerList()[2]) ==
                "fa4b61c4b5d50bfe270f3fc5d486a4e262f87dcea20157204374a026fe94c2f17d4e6248fd93c89938"
                "5c0bce8029fcf1e2a8b8aa1faf083f2ae88f4d971bdd19");
    BOOST_CHECK(*toHexString(decodedBlockHeader->sealerList()[3]) ==
                "deeb46a1b389fa1e4b225df5c9ced5cffb4cb8c7ad2729eb78b5d99e59b4c64eaa654e7e955c612998"
                "954f5463f25a8e7b6e26a61a100e0ac68b629fa4159282");

    BOOST_CHECK(*toHexString(decodedBlockHeader->signatureList()[0].signature) ==
                "9529fc4f0064656104f209706d4d20d86637c30c2ec35ed9248695ee453b06754251494fb0ff8f2e9f"
                "779c884dbf17d9d97c4b126fb5c7c39c7b6adb44f8a79a");
    BOOST_CHECK(*toHexString(decodedBlockHeader->signatureList()[1].signature) ==
                "f5af51722e4519709f430d20ea3cf1158dffd30fae01b7548d7c52733217587f31948d405d0792e9b4"
                "b852757fcd92e876f645818dd493cce3fb6310a073dcbd");
    BOOST_CHECK(*toHexString(decodedBlockHeader->signatureList()[2].signature) ==
                "1483270d9bd8bc98121035d537d84c8ee7a252b5cbd26004cfe8a35b31461df8b90de93453ee713cac"
                "973a559039905a8009613d06941eab935bc017c8fdc1d2");
    BOOST_CHECK(*toHexString(decodedBlockHeader->signatureList()[3].signature) ==
                "d058eca499d70c0c751672f407c263488b28194ea970fe8ed09d03945ae84e75ec2fb395370d648655"
                "2cef01208a4436f9f790ffa16f4d0c0538b08a29dd9c58");
    BOOST_CHECK(decodedBlockHeader->consensusWeights().size() == 0);
#endif
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos