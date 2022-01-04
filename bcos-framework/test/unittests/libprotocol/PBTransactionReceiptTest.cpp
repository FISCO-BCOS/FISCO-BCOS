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
 * @brief test for Transaction
 * @file PBTransactionTest.h
 * @author: yujiechen
 * @date: 2021-03-16
 */
#include "bcos-utilities/testutils/TestPromptFixture.h"
#include "libprotocol/Common.h"
#include "testutils/protocol/FakeTransactionReceipt.h"

using namespace bcos;
using namespace bcos::protocol;
using namespace bcos::crypto;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(PBTransationReceiptTest, TestPromptFixture)
BOOST_AUTO_TEST_CASE(testNormalPBransactionReceipt)
{
    auto hashImpl = std::make_shared<Keccak256Hash>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, nullptr, nullptr);
    testPBTransactionReceipt(cryptoSuite);
}
BOOST_AUTO_TEST_CASE(testSMPBTransactionReceipt)
{
    auto hashImpl = std::make_shared<Sm3Hash>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, nullptr, nullptr);
    testPBTransactionReceipt(cryptoSuite);
}

BOOST_AUTO_TEST_CASE(testNormalPBTransactionRecept)
{
    std::cout << "###### testNormalPBTransactionRecept" << std::endl;
    auto hashImpl = std::make_shared<Keccak256Hash>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, nullptr, nullptr);
#if 0
// FIXME: correct this test when the receipt is fixed
    auto receiptData = fromHexString(
        "12d9050b00000071035fe3c4c3e2079879a0dba1937aca95ac16e68f0f5fe3c4c3e2079879a0dba1937aca95ac"
        "16e68f0f5fe3c4c3e2079879a0dba1937aca95ac16e68f0f5fe3c4c3e2079879a0dba1937aca95ac16e68f0f5f"
        "e3c4c3e2079879a0dba1937aca95ac16e68f0f5fe3c4c3e2079879a0dba1937aca95ac16e68f0f5fe3c4c3e207"
        "9879a0dba1937aca95ac16e68f0f5fe3c4c3e2079879a0dba1937aca95ac16e68f0f5fe3c4c3e2079879a0dba1"
        "937aca95ac16e68f0f5fe3c4c3e2079879a0dba1937aca95ac16e68f0f5fe3c4c3e2079879a0dba1937aca95ac"
        "16e68f0f505fe3c4c3e2079879a0dba1937aca95ac16e68f0f808ea407c24ec9663ee0e7a5f2d0e9afffec2808"
        "7c25f296e68585bb292ed6c5e50766eeb6df020104000000000000000000000000000000000000000000000000"
        "100000000004000000000000000000000000000000000000000000000000000000000004100000000000000000"
        "000000000000000000000000000000080000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000800000000002000000000000000000000000000000000200000000000000000000080000000000000000"
        "000000000000000000000000000000000000000200000000080000000000000000000000000000000000000000"
        "0000000001000008502863c51de9fcb96542a07186fe3aeda6bb8a116d0480044852b2a670ade5407e78fb2863"
        "c51de9fcb96542a07186fe3aeda6bb8a116d80044852b2a670ade5407e78fb2863c51de9fcb96542a07186fe3a"
        "eda6bb8a116d5082df0950f5a951637e0307cdcb4c672f298b8bc60480c89efdaa54c0f20c7adf612882df0950"
        "f5a951637e0307cdcb4c672f298b8bc680c89efdaa54c0f20c7adf612882df0950f5a951637e0307cdcb4c672f"
        "298b8bc60000000000000000");
    auto receipt = std::make_shared<PBTransactionReceipt>(cryptoSuite, *receiptData);
    BOOST_CHECK(receipt->gasUsed() == 12343242342);
    BOOST_CHECK(
        *toHexString(receipt->output()) ==
        "5fe3c4c3e2079879a0dba1937aca95ac16e68f0f5fe3c4c3e2079879a0dba1937aca95ac16e68f0f5fe3c4c3e2"
        "079879a0dba1937aca95ac16e68f0f5fe3c4c3e2079879a0dba1937aca95ac16e68f0f5fe3c4c3e2079879a0db"
        "a1937aca95ac16e68f0f5fe3c4c3e2079879a0dba1937aca95ac16e68f0f5fe3c4c3e2079879a0dba1937aca95"
        "ac16e68f0f5fe3c4c3e2079879a0dba1937aca95ac16e68f0f5fe3c4c3e2079879a0dba1937aca95ac16e68f0f"
        "5fe3c4c3e2079879a0dba1937aca95ac16e68f0f5fe3c4c3e2079879a0dba1937aca95ac16e68f0f");
    BOOST_CHECK(
        *toHexString(receipt->contractAddress()) == "5fe3c4c3e2079879a0dba1937aca95ac16e68f0f");
    BOOST_CHECK(receipt->status() == (int32_t)TransactionStatus::BadJumpDestination);
    BOOST_CHECK(receipt->hash().hex() ==
                "75d73d5cb2ff395fcd645641ce9e41873854bf64b69b4ecae98917f9d0935218");
    auto& logEntry = (receipt->logEntries())[1];
    BOOST_CHECK((logEntry.topics()[0]).hex() ==
                "c89efdaa54c0f20c7adf612882df0950f5a951637e0307cdcb4c672f298b8bc6");
    BOOST_CHECK(*toHexString(logEntry.address()) == "82df0950f5a951637e0307cdcb4c672f298b8bc6");
    BOOST_CHECK(*toHexString(logEntry.data()) ==
                "c89efdaa54c0f20c7adf612882df0950f5a951637e0307cdcb4c672f298b8bc6");

    // check exception case
    (*receiptData)[0] += 1;
    BOOST_CHECK_THROW(
        std::make_shared<PBTransactionReceipt>(cryptoSuite, *receiptData), PBObjectDecodeException);
#endif
}

BOOST_AUTO_TEST_CASE(testSMPBTransactionRecept)
{
    std::cout << "###### testSMPBTransactionRecept" << std::endl;
    auto hashImpl = std::make_shared<Sm3Hash>();
    auto cryptoSuite = std::make_shared<CryptoSuite>(hashImpl, nullptr, nullptr);
#if 0
// FIXME: correct this test when the receipt is fixed
    auto receiptData = fromHexString(
        "12d9050b00000071035fe3c4c3e2079879a0dba1937aca95ac16e68f0f5fe3c4c3e2079879a0dba1937aca95ac"
        "16e68f0f5fe3c4c3e2079879a0dba1937aca95ac16e68f0f5fe3c4c3e2079879a0dba1937aca95ac16e68f0f5f"
        "e3c4c3e2079879a0dba1937aca95ac16e68f0f5fe3c4c3e2079879a0dba1937aca95ac16e68f0f5fe3c4c3e207"
        "9879a0dba1937aca95ac16e68f0f5fe3c4c3e2079879a0dba1937aca95ac16e68f0f5fe3c4c3e2079879a0dba1"
        "937aca95ac16e68f0f5fe3c4c3e2079879a0dba1937aca95ac16e68f0f5fe3c4c3e2079879a0dba1937aca95ac"
        "16e68f0f505fe3c4c3e2079879a0dba1937aca95ac16e68f0f800f000c0d3cf849632ed9aa2925251c0a13ee4f"
        "09f991b30bf396c15bf7ae80970766eeb6df020104000000100000000000000000000000000000000000000000"
        "000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000000000000000000000000401000010000000000000000000000000000000000000000000000000000000"
        "000000000000000000400000000000000000000000000000000000000000000000000000000010000000000000"
        "000000000000000000000000000000100000000000000000000000000000000010000000000000040000000000"
        "000000000020000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "00040020000000085072e58843de1eed164ed526c3b56b22c2b47324a0048006d47b6f2f121e85160d1d8072e5"
        "8843de1eed164ed526c3b56b22c2b47324a08006d47b6f2f121e85160d1d8072e58843de1eed164ed526c3b56b"
        "22c2b47324a050d7d75330538a6882f5dfdc3b64115c647f3328c40480cbdddb8e8421b23498480570d7d75330"
        "538a6882f5dfdc3b64115c647f3328c480cbdddb8e8421b23498480570d7d75330538a6882f5dfdc3b64115c64"
        "7f3328c40000000000000000");
    auto receipt = std::make_shared<PBTransactionReceipt>(cryptoSuite, *receiptData);
    BOOST_CHECK(receipt->gasUsed() == 12343242342);
    BOOST_CHECK(
        *toHexString(receipt->output()) ==
        "5fe3c4c3e2079879a0dba1937aca95ac16e68f0f5fe3c4c3e2079879a0dba1937aca95ac16e68f0f5fe3c4c3e2"
        "079879a0dba1937aca95ac16e68f0f5fe3c4c3e2079879a0dba1937aca95ac16e68f0f5fe3c4c3e2079879a0db"
        "a1937aca95ac16e68f0f5fe3c4c3e2079879a0dba1937aca95ac16e68f0f5fe3c4c3e2079879a0dba1937aca95"
        "ac16e68f0f5fe3c4c3e2079879a0dba1937aca95ac16e68f0f5fe3c4c3e2079879a0dba1937aca95ac16e68f0f"
        "5fe3c4c3e2079879a0dba1937aca95ac16e68f0f5fe3c4c3e2079879a0dba1937aca95ac16e68f0f");
    BOOST_CHECK(
        *toHexString(receipt->contractAddress()) == "5fe3c4c3e2079879a0dba1937aca95ac16e68f0f");
    BOOST_CHECK(receipt->status() == (int32_t)TransactionStatus::BadJumpDestination);
    BOOST_CHECK(receipt->hash().hex() ==
                "0728ad96856d3f355be308a3f452364cb4052e357472e4562c8f60437f8b053a");
    auto& logEntry = (receipt->logEntries())[1];
    BOOST_CHECK((logEntry.topics()[0]).hex() ==
                "cbdddb8e8421b23498480570d7d75330538a6882f5dfdc3b64115c647f3328c4");
    BOOST_CHECK(*toHexString(logEntry.address()) == "d7d75330538a6882f5dfdc3b64115c647f3328c4");
    BOOST_CHECK(*toHexString(logEntry.data()) ==
                "cbdddb8e8421b23498480570d7d75330538a6882f5dfdc3b64115c647f3328c4");
    // check exception case
    (*receiptData)[0] += 1;
    BOOST_CHECK_THROW(
        std::make_shared<PBTransactionReceipt>(cryptoSuite, *receiptData), PBObjectDecodeException);
#endif
}
BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos