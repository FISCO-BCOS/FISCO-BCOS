/*
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @brief executor_version 0 with feature_evm_prague on and
 *        bugfix_evm_create2_delegatecall_staticcall_codecopy off: every CALL-family opcode makes
 *        evmone probe the target for an EIP-7702 delegation through host copy_code with an
 *        uninitialized 23-byte stack buffer. The legacy host copyCode branch stored that buffer
 *        as the CALLING contract's code: the contract was destroyed after its first call, and
 *        nodes whose stack held different bytes wrote different state.
 */
#include "bcos-framework/executor/ExecutionMessage.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include "evmone/compat/CompatExecutorSmokeHarness.h"
#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>

namespace bcos::test
{
namespace
{
using Flag = ledger::Features::Flag;
constexpr std::string_view c_copyCodeSender = "11ac3ca85a307ae2aff614e83949ab691ba019c5";
// CALL(GAS, CALLDATALOAD(0), 0, 0, 0, 0, 0), return the success word
constexpr std::string_view c_callRuntime = "600060006000600060006000355af160005260206000f3";
// STATICCALL(GAS, CALLDATALOAD(0), calldata[32:]) and return the returndata
constexpr std::string_view c_staticCallRuntime =
    "60006000366020900380602060003760006000355afa3d600060003e3d6000f3";
// EXTCODECOPY(CALLDATALOAD(0), 0, 0, 4), RETURN(0, 4)
constexpr std::string_view c_extCodeCopyRuntime = "6004600060006000353c60046000f3";
// ecrecover and sha256 precompiles
constexpr std::string_view c_ecrecover = "0000000000000000000000000000000000000001";
constexpr std::string_view c_sha256 = "0000000000000000000000000000000000000002";

bytes fromHexString(std::string_view hex)
{
    bytes out;
    boost::algorithm::unhex(hex.begin(), hex.end(), std::back_inserter(out));
    return out;
}

// init code that CODECOPYs the bytes after this 12-byte header and RETURNs them
bytes initCodeFor(std::string_view runtimeHex)
{
    auto runtime = fromHexString(runtimeHex);
    auto size = static_cast<uint8_t>(runtime.size());
    bytes code{0x60, size, 0x60, 0x0c, 0x60, 0x00, 0x39, 0x60, size, 0x60, 0x00, 0xf3};
    code.insert(code.end(), runtime.begin(), runtime.end());
    return code;
}

// calldata: the target address as a left-padded word, then the forwarded input
bytes callData(std::string_view targetHex, bytes const& input = {})
{
    auto address = fromHexString(targetHex);
    bytes data(32 - address.size(), 0);
    data.insert(data.end(), address.begin(), address.end());
    data.insert(data.end(), input.begin(), input.end());
    return data;
}

struct V0PragueCopyCodeFixture : CompatExecutorSmokeFixture
{
    protocol::BlockNumber m_number = 0;
    bool m_dmcRevert = false;
    // each deployment gets its own context, so its address does not collide with an earlier one
    int64_t m_deployContext = 100;

    static ledger::Features features(bool prague, bool dmcRevert)
    {
        ledger::Features features;
        features.set(Flag::feature_evm_cancun);
        if (prague)
        {
            features.set(Flag::feature_evm_prague);
        }
        if (dmcRevert)
        {
            features.set(Flag::bugfix_dmc_revert);
        }
        return features;
    }

    ExecutorSmokeEnv chain(
        ledger::Features const& features, protocol::BlockNumber number, bool dmcRevert = false)
    {
        m_number = number;
        m_dmcRevert = dmcRevert;
        auto env = buildExecutorWithFeatures(features);
        task::syncWait(ledger::writeToStorage(features, *env.storage, number));
        auto blockHeader = std::make_shared<bcostars::protocol::BlockHeaderImpl>();
        blockHeader->setNumber(number);
        blockHeader->setVersion(static_cast<uint32_t>(protocol::BlockVersion::V3_18_0_VERSION));
        blockHeader->setParentInfo({.blockNumber = number - 1, .blockHash = h256(number - 1)});
        ledger->setBlockNumber(number - 1);
        blockHeader->calculateHash(*cryptoSuite->hashImpl());
        std::promise<void> next;
        env.executor->nextBlockHeader(0, blockHeader, [&next](Error::Ptr&& error) {
            BOOST_CHECK(!error);
            next.set_value();
        });
        next.get_future().get();
        return env;
    }

    NativeExecutionMessage::UniquePtr message(std::string to, bytes input, int64_t contextID)
    {
        auto params = std::make_unique<NativeExecutionMessage>();
        params->setType(NativeExecutionMessage::MESSAGE);
        params->setContextID(contextID);
        params->setSeq(0);
        params->setDepth(0);
        params->setOrigin(std::string(c_copyCodeSender));
        params->setFrom(std::string(c_copyCodeSender));
        if (to.empty())
        {
            params->setCreate(true);
            params->setTo(newEVMAddress(hashImpl, m_number, contextID, 0));
        }
        else
        {
            params->setTo(std::move(to));
        }
        params->setGasAvailable(gas);
        params->setData(std::move(input));
        return params;
    }

    bytes code(ExecutorSmokeEnv& env, std::string_view address)
    {
        std::promise<bytes> promise;
        env.executor->getCode(address, [&promise](Error::Ptr error, bytes code) {
            BOOST_CHECK(!error);
            promise.set_value(std::move(code));
        });
        return promise.get_future().get();
    }

    protocol::ExecutionMessage::UniquePtr serial(
        ExecutorSmokeEnv& env, NativeExecutionMessage::UniquePtr params)
    {
        std::vector<protocol::ExecutionMessage::UniquePtr> inputs;
        inputs.emplace_back(std::move(params));
        std::promise<std::vector<protocol::ExecutionMessage::UniquePtr>> promise;
        env.executor->executeTransactions(protocol::SERIAL_EXECUTIVE_FLOW_ADDRESS, inputs,
            [&promise](Error::UniquePtr error,
                std::vector<protocol::ExecutionMessage::UniquePtr> outputs) {
                BOOST_CHECK(!error);
                promise.set_value(std::move(outputs));
            });
        auto outputs = promise.get_future().get();
        BOOST_REQUIRE_EQUAL(outputs.size(), 1U);
        return std::move(outputs.front());
    }

    // Drives dmcExecuteTransaction and answers a GET_CODE request the way DmcExecutor does:
    // FINISHED on the requesting frame's seq, or PRE_FINISH under bugfix_dmc_revert.
    protocol::ExecutionMessage::UniquePtr dmc(
        ExecutorSmokeEnv& env, NativeExecutionMessage::UniquePtr params)
    {
        protocol::ExecutionMessage::UniquePtr next = std::move(params);
        while (true)
        {
            auto result = dmcExecute(
                *env.executor, std::unique_ptr<NativeExecutionMessage>(
                                   static_cast<NativeExecutionMessage*>(next.release())));
            if (result->type() != protocol::ExecutionMessage::MESSAGE ||
                result->data().toBytes() != protocol::GET_CODE_INPUT_BYTES)
            {
                return result;
            }
            result->setData(code(env, result->delegateCallAddress()));
            result->setType(m_dmcRevert ? protocol::ExecutionMessage::PRE_FINISH :
                                          protocol::ExecutionMessage::FINISHED);
            result->setCreate(false);
            next = std::move(result);
        }
    }

    protocol::ExecutionMessage::UniquePtr exec(
        bool useDmc, ExecutorSmokeEnv& env, NativeExecutionMessage::UniquePtr params)
    {
        return useDmc ? dmc(env, std::move(params)) : serial(env, std::move(params));
    }

    std::string deploy(bool useDmc, ExecutorSmokeEnv& env, std::string_view runtimeHex)
    {
        auto deployed = exec(useDmc, env, message({}, initCodeFor(runtimeHex), m_deployContext++));
        BOOST_REQUIRE_EQUAL(deployed->status(), 0);
        BOOST_REQUIRE_EQUAL(toHex(code(env, deployed->newEVMContractAddress())), runtimeHex);
        return std::string(deployed->newEVMContractAddress());
    }

    // A CALL and a STATICCALL each leave the caller's code as deployed, and the call succeeds.
    void callsKeepCallerCode(bool useDmc)
    {
        auto env = chain(features(true, m_dmcRevert), m_number, m_dmcRevert);

        auto caller = deploy(useDmc, env, c_callRuntime);
        auto called = exec(useDmc, env, message(caller, callData(c_ecrecover), 1));
        BOOST_CHECK_EQUAL(called->status(), 0);
        BOOST_CHECK_EQUAL(toHex(code(env, caller)), c_callRuntime);

        auto proxy = deploy(useDmc, env, c_staticCallRuntime);
        auto first = exec(useDmc, env, message(proxy, callData(c_ecrecover, bytes(128)), 2));
        BOOST_CHECK_EQUAL(first->status(), 0);
        BOOST_CHECK_EQUAL(toHex(code(env, proxy)), c_staticCallRuntime);
        // a second call still runs the deployed proxy: sha256("abc")
        auto second = exec(useDmc, env, message(proxy, callData(c_sha256, {'a', 'b', 'c'}), 3));
        BOOST_CHECK_EQUAL(second->status(), 0);
        BOOST_CHECK_EQUAL(toHex(second->data()),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        BOOST_CHECK_EQUAL(toHex(code(env, proxy)), c_staticCallRuntime);
    }
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(V0PragueCopyCodeTest, V0PragueCopyCodeFixture)

BOOST_AUTO_TEST_CASE(serialCallKeepsCallerCode)
{
    m_number = 13'000'001;
    callsKeepCallerCode(false);
}

BOOST_AUTO_TEST_CASE(dmcCallKeepsCallerCode)
{
    m_number = 13'000'002;
    callsKeepCallerCode(true);
}

BOOST_AUTO_TEST_CASE(dmcRevertCallKeepsCallerCode)
{
    m_number = 13'000'003;
    m_dmcRevert = true;
    callsKeepCallerCode(true);
}

// From Prague on, EXTCODECOPY reads the target's code into memory too.
BOOST_AUTO_TEST_CASE(pragueExtCodeCopyKeepsCallerCode)
{
    auto env = chain(features(true, false), 13'000'004);
    auto copier = deploy(false, env, c_extCodeCopyRuntime);
    auto copied = exec(false, env, message(copier, callData(copier), 1));
    BOOST_CHECK_EQUAL(copied->status(), 0);
    BOOST_CHECK_EQUAL(toHex(copied->data()), c_extCodeCopyRuntime.substr(0, 8));
    BOOST_CHECK_EQUAL(toHex(code(env, copier)), c_extCodeCopyRuntime);
}

// Before Prague the legacy host path stays as it was, so replaying an old block gives the same
// state: EXTCODECOPY of 4 bytes replaces the caller's code with the 4 zeroed memory bytes, and a
// CALL never reaches copy_code.
BOOST_AUTO_TEST_CASE(prePragueKeepsLegacyCopyCode)
{
    auto env = chain(features(false, false), 13'000'005);
    auto caller = deploy(false, env, c_callRuntime);
    auto called = exec(false, env, message(caller, callData(c_ecrecover), 1));
    BOOST_CHECK_EQUAL(called->status(), 0);
    BOOST_CHECK_EQUAL(toHex(code(env, caller)), c_callRuntime);

    auto copier = deploy(false, env, c_extCodeCopyRuntime);
    auto copied = exec(false, env, message(copier, callData(caller), 2));
    BOOST_CHECK_EQUAL(copied->status(), 0);
    BOOST_CHECK_EQUAL(toHex(copied->data()), "00000000");
    BOOST_CHECK_EQUAL(toHex(code(env, copier)), "00000000");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
