/**
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @file PrecompiledCallTest.cpp
 * @author: kyonGuo
 * @date 2022/7/26
 */

#include "libprecompiled/PreCompiledFixture.h"
#include "precompiled/ConsensusPrecompiled.h"
#include "precompiled/SystemConfigPrecompiled.h"
#include <bcos-utilities/testutils/TestPromptFixture.h>

using namespace bcos;
using namespace bcos::precompiled;
using namespace bcos::executor;
using namespace bcos::storage;

namespace bcos::test
{
class PrecompiledCallTestFixture : public PrecompiledFixture
{
public:
    PrecompiledCallTestFixture()
    {
        codec = std::make_shared<CodecWrapper>(hashImpl, false);
        setIsWasm(false);
        contractAddress = Address("0x420f853b49838bd3e9466c85a4cc3428c960dde2").hex();
    }

    virtual ~PrecompiledCallTestFixture() = default;

    ExecutionMessage::UniquePtr openTable(
        protocol::BlockNumber _number, const std::string& tableName, int _errorCode = 0)
    {
        nextBlock(_number);
        bytes in = codec->encodeWithSig("openTable(string)", tableName);
        auto tx = fakeTransaction(cryptoSuite, keyPair, "", in, 100, 10000, "1", "1");
        sender = boost::algorithm::hex_lower(std::string(tx->sender()));
        auto hash = tx->hash();
        txpool->hash2Transaction.emplace(hash, tx);
        auto params2 = std::make_unique<NativeExecutionMessage>();
        params2->setTransactionHash(hash);
        params2->setContextID(100);
        params2->setSeq(1000);
        params2->setDepth(0);
        params2->setFrom(sender);
        params2->setTo(isWasm ? TABLE_MANAGER_NAME : TABLE_MANAGER_ADDRESS);
        params2->setOrigin(sender);
        params2->setStaticCall(false);
        params2->setGasAvailable(gas);
        params2->setData(std::move(in));
        params2->setType(NativeExecutionMessage::TXHASH);

        // call precompiled
        std::promise<ExecutionMessage::UniquePtr> executePromise2;
        executor->dmcExecuteTransaction(std::move(params2),
            [&](bcos::Error::UniquePtr&& error, ExecutionMessage::UniquePtr&& result) {
                BOOST_CHECK(!error);
                executePromise2.set_value(std::move(result));
            });
        auto result2 = executePromise2.get_future().get();

        if (_errorCode != 0)
        {
            BOOST_CHECK(result2->data().toBytes() == codec->encode(s256(_errorCode)));
        }
        commitBlock(_number);
        return result2;
    };

    std::string sender;
    std::string contractAddress;
};

BOOST_FIXTURE_TEST_SUITE(precompiledCallTest, PrecompiledCallTestFixture)

BOOST_AUTO_TEST_CASE(precompiled_gas_test)
{
    // call precompiled gas overflow
    gas = 1;
    BlockNumber number = 1;
    auto result = openTable(number++, "error_test");
    BOOST_CHECK(result->status() == (int32_t)TransactionStatus::OutOfGas);
}


BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
