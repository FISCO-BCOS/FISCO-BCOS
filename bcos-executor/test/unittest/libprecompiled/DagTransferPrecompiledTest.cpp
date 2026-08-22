/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

#include "libprecompiled/PreCompiledFixture.h"

#include <boost/test/unit_test.hpp>
using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;

namespace bcos::test
{
class DagTransferPrecompiledFixture : public PrecompiledFixture
{
public:
    DagTransferPrecompiledFixture()
    {
        // V3_0 makes the executor create the DagTransfer test table in the
        // backend at build (TransactionExecutor init), so openTable is stable
        // across blocks instead of depending on genesis state layering.
        // setIsWasm(isWasm, isCheckAuth, isKeyPage, version) became prepareEnv(...) when WASM
        // support was removed (#5348); the leading isWasm=false argument no longer exists.
        prepareEnv(false, false, protocol::BlockVersion::V3_0_VERSION);
    }
    ~DagTransferPrecompiledFixture() override = default;

    // Drive the DagTransfer precompiled (address 100c) through the executor, the
    // same way the BFS list()/initBfs() helpers do. The precompiled operates on
    // its own DAG_TRANSFER table (created by initTestPrecompiledTable at build),
    // so no auth/external-call setup is needed.
    ExecutionMessage::UniquePtr callDag(protocol::BlockNumber _number, bytes&& in)
    {
        auto tx =
            fakeTransaction(cryptoSuite, keyPair, "", in, std::to_string(101), 100001, "1", "1");
        auto sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params = std::make_unique<NativeExecutionMessage>();
        params->setTransactionHash(hash);
        params->setContextID(_number);  // unique per block — avoids DMC cache collisions
        params->setSeq(1000);
        params->setDepth(0);
        params->setFrom(sender);
        params->setTo(std::string(precompiled::DAG_TRANSFER_ADDRESS));
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

    // the precompiled encodes the int return code as u256(ret); decode it as a
    // signed s256 so negative error codes compare correctly.
    s256 decodeRet(ExecutionMessage::UniquePtr const& result)
    {
        s256 ret;
        codec->decode(result->data(), ret);
        return ret;
    }
};

// NOTE: this MockTransactionalStorage returns openTable=null non-deterministically
// once a few committed writes accumulate, and that leaks across runs/fixtures, so
// stateful success paths (the table read/write bodies) can't be asserted reliably
// here. These cases drive the call() dispatch plus every method's argument decode
// and pre-openTable validation, which are deterministic. userTransfer(self,self)
// is a genuine success path that returns before opening the table.
BOOST_FIXTURE_TEST_SUITE(DagTransferPrecompiledTest, DagTransferPrecompiledFixture)

BOOST_AUTO_TEST_CASE(emptyUserNameRejectedByEveryMethod)
{
    protocol::BlockNumber number = 2;

    // every mutating method rejects an empty user name with CODE_INVALID_USER_NAME
    // before it ever opens the table — deterministic regardless of table state.
    BOOST_CHECK_LT(decodeRet(callDag(number++,
                       codec->encodeWithSig("userAdd(string,uint256)", std::string(""), u256(1)))),
        0);
    BOOST_CHECK_LT(decodeRet(callDag(number++,
                       codec->encodeWithSig("userSave(string,uint256)", std::string(""), u256(1)))),
        0);
    BOOST_CHECK_LT(decodeRet(callDag(number++,
                       codec->encodeWithSig("userDraw(string,uint256)", std::string(""), u256(1)))),
        0);
    BOOST_CHECK_LT(
        decodeRet(callDag(number++, codec->encodeWithSig("userTransfer(string,string,uint256)",
                                        std::string(""), std::string("y"), u256(1)))),
        0);
    {
        auto result =
            callDag(number++, codec->encodeWithSig("userBalance(string)", std::string("")));
        s256 ret;
        u256 balance;
        codec->decode(result->data(), ret, balance);
        BOOST_CHECK_LT(ret, 0);
    }
}

BOOST_AUTO_TEST_CASE(zeroAmountRejected)
{
    // amount check runs after the name check, still before openTable
    BOOST_CHECK_LT(decodeRet(callDag(2, codec->encodeWithSig("userSave(string,uint256)",
                                            std::string("x"), u256(0)))),
        0);
    BOOST_CHECK_LT(decodeRet(callDag(3, codec->encodeWithSig("userDraw(string,uint256)",
                                            std::string("x"), u256(0)))),
        0);
}

BOOST_AUTO_TEST_CASE(transferToSelfReturnsBeforeTable)
{
    // fromUser == toUser short-circuits to success without touching the table
    BOOST_CHECK_EQUAL(
        decodeRet(callDag(2, codec->encodeWithSig("userTransfer(string,string,uint256)",
                                 std::string("self"), std::string("self"), u256(5)))),
        0);
}

BOOST_AUTO_TEST_CASE(unknownSelectorIsIgnored)
{
    // a selector that matches no method falls through to the no-op else branch
    BOOST_CHECK_NO_THROW(callDag(2, codec->encodeWithSig("noSuchMethod(uint256)", u256(1))));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
