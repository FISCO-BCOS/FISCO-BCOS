/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "precompiled/extension/SmallBankPrecompiled.h"
#include "libprecompiled/PreCompiledFixture.h"

#include <boost/test/unit_test.hpp>
using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;

namespace bcos::test
{
class SmallBankPrecompiledFixture : public PrecompiledFixture
{
public:
    SmallBankPrecompiledFixture()
    {
        // V3_0 makes the executor create the SmallBank test tables in the backend
        // at build, so the precompiled is registered and routable.
        // setIsWasm(isWasm, isCheckAuth, isKeyPage, version) became prepareEnv(...) when WASM
        // support was removed (#5348); the leading isWasm=false argument no longer exists.
        prepareEnv(false, false, protocol::BlockVersion::V3_0_VERSION);
    }
    ~SmallBankPrecompiledFixture() override = default;

    // SmallBankPrecompiled lives at a computed address (getAddress(0)) and wipes
    // its exec result before returning, so only NO_THROW is assertable. The
    // executor still runs the full method body, which is what we cover. As with
    // DagTransfer, the mock storage's openTable is flaky across writes, so we keep
    // assertions lenient.
    ExecutionMessage::UniquePtr callSmallBank(protocol::BlockNumber _number, bytes&& in)
    {
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        auto sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params = std::make_unique<NativeExecutionMessage>();
        params->setTransactionHash(hash);
        params->setContextID(_number);
        params->setSeq(1000);
        params->setDepth(0);
        params->setFrom(sender);
        params->setTo(SmallBankPrecompiled::getAddress(0));  // first smallbank contract
        params->setOrigin(sender);
        params->setStaticCall(false);
        params->setGasAvailable(gas);
        params->setData(std::move(in));
        params->setType(NativeExecutionMessage::TXHASH);
        nextBlock(_number, m_blockVersion);

        std::promise<ExecutionMessage::UniquePtr> executePromise;
        executor->dmcExecuteTransaction(std::move(params),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise.set_value(std::move(result));
            });
        auto result = executePromise.get_future().get();
        commitBlock(_number);
        return result;
    }
};

BOOST_FIXTURE_TEST_SUITE(SmallBankPrecompiledTest, SmallBankPrecompiledFixture)

BOOST_AUTO_TEST_CASE(updateBalanceAndSendPayment)
{
    // updateBalance creates a user; a second call hits the already-exist branch
    BOOST_CHECK_NO_THROW(callSmallBank(
        2, codec->encodeWithSig("updateBalance(string,uint256)", std::string("alice"), u256(100))));
    BOOST_CHECK_NO_THROW(callSmallBank(
        3, codec->encodeWithSig("updateBalance(string,uint256)", std::string("bob"), u256(50))));
    // sendPayment between two users
    BOOST_CHECK_NO_THROW(callSmallBank(4, codec->encodeWithSig("sendPayment(string,string,uint256)",
                                              std::string("alice"), std::string("bob"), u256(20))));
}

BOOST_AUTO_TEST_CASE(validationPaths)
{
    protocol::BlockNumber number = 2;
    // empty user name on updateBalance → CODE_INVALID_USER_NAME branch
    BOOST_CHECK_NO_THROW(callSmallBank(
        number++, codec->encodeWithSig("updateBalance(string,uint256)", std::string(""), u256(1))));
    // empty users on sendPayment → invalid name branch
    BOOST_CHECK_NO_THROW(
        callSmallBank(number++, codec->encodeWithSig("sendPayment(string,string,uint256)",
                                    std::string(""), std::string("y"), u256(1))));
    // zero amount on sendPayment → invalid amount branch
    BOOST_CHECK_NO_THROW(
        callSmallBank(number++, codec->encodeWithSig("sendPayment(string,string,uint256)",
                                    std::string("a"), std::string("b"), u256(0))));
    // self payment → short-circuit success branch
    BOOST_CHECK_NO_THROW(
        callSmallBank(number++, codec->encodeWithSig("sendPayment(string,string,uint256)",
                                    std::string("s"), std::string("s"), u256(5))));
    // sendPayment from a non-existent user → not-exist branch
    BOOST_CHECK_NO_THROW(
        callSmallBank(number++, codec->encodeWithSig("sendPayment(string,string,uint256)",
                                    std::string("ghost"), std::string("z"), u256(1))));
    // unknown selector → no-op else branch
    BOOST_CHECK_NO_THROW(
        callSmallBank(number++, codec->encodeWithSig("noSuchMethod(uint256)", u256(1))));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
