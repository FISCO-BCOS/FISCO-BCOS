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
 * @brief Gas characterization of the executor_version 0 scheduler: the budget BlockExecutive
 *        hands each transaction, the gasUsed it writes into the receipt and the block's gasUsed,
 *        under each combination of the three flags it reads, for the DMC and the serial block
 *        executive. Pinned before the gas rules move into FiscoGasPolicy
 *        (docs/adr/0012-gas-metering-policy-per-lane.md, section "特征测试（PR 1）").
 *
 * The executor is a stub that charges every frame 1000 gas, so what the table shows is the
 * scheduler's own arithmetic. GasCharacterization_V0Test in bcos-executor pins the executor.
 *
 * Regenerate the table only for an intended behaviour change:
 *   GAS_CHARACTERIZATION_RECORD=1 test-scheduler --run_test=GasCharacterization_SchedulerTest
 */
// FakeBlock.h pulls in range-v3 and has to come before mock/MockDmcExecutor.h, which opens
// namespace std at file scope and so makes ::ranges ambiguous for anything included after it.
// clang-format off
#include <bcos-framework/testutils/faker/FakeBlock.h>
// clang-format on
#include "bcos-framework/executor/PrecompiledTypeDef.h"
#include "bcos-framework/testutils/GasCharacterization.h"
#include "bcos-protocol/bcos-protocol/TransactionSubmitResultFactoryImpl.h"
#include "bcos-scheduler/src/BlockExecutive.h"
#include "bcos-scheduler/src/SchedulerImpl.h"
#include "bcos-scheduler/src/SerialBlockExecutive.h"
#include "mock/MockLedger3.h"
#include "mock/MockTxPool1.h"
// clang-format off
#include "mock/MockDmcExecutor.h"
// clang-format on
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-crypto/interfaces/crypto/CryptoSuite.h>
#include <bcos-crypto/signature/secp256k1/Secp256k1Crypto.h>
#include <bcos-framework/executor/NativeExecutionMessage.h>
#include <bcos-tars-protocol/protocol/BlockFactoryImpl.h>
#include <bcos-tars-protocol/protocol/BlockHeaderFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-utilities/IOServicePool.h>
#include <boost/test/unit_test.hpp>
#include <future>
#include <mutex>

namespace bcos::test
{
namespace
{
using namespace gas_characterization;
using Flag = ledger::Features::Flag;

constexpr int64_t c_frameCost = 1000;
const std::string c_plainTarget = "0000000000000000000000000000000000005001";
const std::string c_systemTarget = std::string(precompiled::SYS_CONFIG_ADDRESS);

// The three gas flags BlockExecutive reads (ADR 0012, section "FiscoGasRules 字段": budgetSource
// and zeroGasUsedForSystemContract); every combination of them.
const std::vector<std::pair<std::string_view, Flag>> c_flags = {
    {"gascalc", Flag::bugfix_precompiled_gascalc},
    {"gasused", Flag::bugfix_precompiled_gasused},
    {"dmcDeploy", Flag::bugfix_dmc_deploy_gas_used},
};

// A receipt's and a header's gasUsed are u256. The scheduler can store a wrapped negative
// difference there (a budget larger than the one it subtracts from), so the table keeps the low
// 64 bits rather than a saturated value.
int64_t lowBits(u256 const& value)
{
    return static_cast<int64_t>(
        static_cast<uint64_t>(value & u256(std::numeric_limits<uint64_t>::max())));
}

// Serves the configuration's flags; MockLedger3 has none.
class FeatureLedger : public MockLedger3
{
public:
    explicit FeatureLedger(ledger::Features features) : m_features(std::move(features)) {}

    task::Task<ledger::Features> fetchAllFeatures(protocol::BlockNumber /*blockNumber*/) override
    {
        co_return m_features;
    }

private:
    ledger::Features m_features;
};

// Charges every frame c_frameCost and answers the way the executor answers a finished
// transaction: from the contract back to the sender. A transaction whose input is "deploy"
// reports a new contract address, as a deployment made from a system contract does.
class ChargingExecutor : public MockDmcExecutor
{
public:
    ChargingExecutor() : MockDmcExecutor("executor1") {}

    // written from the block executive's per-contract executor threads
    std::mutex mutex;
    std::map<int64_t, int64_t> budgets;

    int64_t budget(int64_t contextID)
    {
        std::scoped_lock lock(mutex);
        return budgets.at(contextID);
    }

    void finish(protocol::ExecutionMessage& message)
    {
        {
            std::scoped_lock lock(mutex);
            budgets[message.contextID()] = message.gasAvailable();
        }
        const auto deploy = message.data().toBytes() == bytes{'d', 'e', 'p', 'l', 'o', 'y'};
        message.setType(protocol::ExecutionMessage::FINISHED);
        message.setStatus(0);
        auto contract = std::string(message.to());
        message.setTo(std::string(message.origin()));
        message.setFrom(contract);
        message.setGasAvailable(std::max<int64_t>(message.gasAvailable() - c_frameCost, 0));
        message.setNewEVMContractAddress(
            deploy || message.create() ? "00000000000000000000000000000000000000dd" : "");
        message.setData({});
        message.setKeyLocks({});
    }

    void executeAll(gsl::span<protocol::ExecutionMessage::UniquePtr> inputs,
        std::function<void(Error::UniquePtr, std::vector<protocol::ExecutionMessage::UniquePtr>)>
            callback)
    {
        std::vector<protocol::ExecutionMessage::UniquePtr> results;
        for (auto& input : inputs)
        {
            finish(*input);
            results.emplace_back(std::move(input));
        }
        callback(nullptr, std::move(results));
    }

    void dmcExecuteTransactions(std::string /*contractAddress*/,
        gsl::span<protocol::ExecutionMessage::UniquePtr> inputs,
        std::function<void(Error::UniquePtr, std::vector<protocol::ExecutionMessage::UniquePtr>)>
            callback) override
    {
        executeAll(inputs, std::move(callback));
    }

    void executeTransactions(std::string /*contractAddress*/,
        gsl::span<protocol::ExecutionMessage::UniquePtr> inputs,
        std::function<void(Error::UniquePtr, std::vector<protocol::ExecutionMessage::UniquePtr>)>
            callback) override
    {
        executeAll(inputs, std::move(callback));
    }
};

struct GasCharacterizationSchedulerFixture
{
    crypto::Hash::Ptr hashImpl = std::make_shared<crypto::Keccak256>();
    crypto::CryptoSuite::Ptr suite = std::make_shared<crypto::CryptoSuite>(
        hashImpl, std::make_shared<crypto::Secp256k1Crypto>(), nullptr);
    std::shared_ptr<bcostars::protocol::TransactionFactoryImpl> transactionFactory =
        std::make_shared<bcostars::protocol::TransactionFactoryImpl>(suite);
    protocol::BlockFactory::Ptr blockFactory =
        std::make_shared<bcostars::protocol::BlockFactoryImpl>(suite,
            std::make_shared<bcostars::protocol::BlockHeaderFactoryImpl>(suite), transactionFactory,
            std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(suite));
    crypto::KeyPairInterface::Ptr keyPair = suite->signatureImpl()->generateKeyPair();
    protocol::TransactionSubmitResultFactory::Ptr submitResultFactory =
        std::make_shared<protocol::TransactionSubmitResultFactoryImpl>();
    std::shared_ptr<MockTxPool1> txPool = std::make_shared<MockTxPool1>();

    struct Tx
    {
        std::string name;
        std::string to;
        std::string input;
        int64_t gasLimit = 0;
    };
    const std::vector<Tx> c_txs = {
        {"plain", c_plainTarget, "call"},
        {"system", c_systemTarget, "call"},
        {"systemDeploy", c_systemTarget, "deploy"},
        {"create", "", "init"},
        // signs for less than the chain's tx_gas_limit; this lane's budget ignores it
        {"capped", c_plainTarget, "call", c_cappedGasLimit},
    };

    void run(std::string const& config, ledger::Features const& features, bool serial,
        Recorder& recorder)
    {
        auto ioServicePool = std::make_shared<IOServicePool>(1, "gas-char");
        boost::asio::io_context ioService;
        auto executorManager = std::make_shared<scheduler::ExecutorManager>(ioService);
        auto executor = std::make_shared<ChargingExecutor>();
        executorManager->addExecutor("executor1", executor);
        auto scheduler = std::make_shared<scheduler::SchedulerImpl>(executorManager,
            std::make_shared<FeatureLedger>(features), nullptr,
            std::make_shared<executor::NativeExecutionMessageFactory>(), blockFactory, txPool,
            submitResultFactory, hashImpl, false, false, serial, 0, ioServicePool);

        auto block = blockFactory->createBlock();
        block->blockHeader()->setNumber(100);
        block->blockHeader()->calculateHash(*hashImpl);
        int nonce = 0;
        for (auto const& tx : c_txs)
        {
            auto input = bytes(tx.input.begin(), tx.input.end());
            block->appendTransaction(
                tx.gasLimit > 0 ?
                    transactionFactory->createTransaction(1, tx.to, input, std::to_string(nonce++),
                        200, "chainID", "groupID", 400, *keyPair, std::string{}, "0x0", "0x0",
                        tx.gasLimit) :
                    transactionFactory->createTransaction(0, tx.to, input, std::to_string(nonce++),
                        200, "chainID", "groupID", 400, *keyPair));
        }

        std::string gasPrice = "0x0";
        std::shared_ptr<scheduler::BlockExecutive> blockExecutive;
        if (serial)
        {
            blockExecutive = std::make_shared<scheduler::SerialBlockExecutive>(block,
                scheduler.get(), 0, submitResultFactory, false, blockFactory, txPool,
                static_cast<uint64_t>(c_chainGasLimit), gasPrice, false);
        }
        else
        {
            blockExecutive = std::make_shared<scheduler::BlockExecutive>(block, scheduler.get(), 0,
                submitResultFactory, false, blockFactory, txPool,
                static_cast<uint64_t>(c_chainGasLimit), gasPrice, false);
        }
        std::promise<protocol::BlockHeader::Ptr> executed;
        blockExecutive->asyncExecute(
            [&executed](Error::UniquePtr error, protocol::BlockHeader::Ptr header, bool) {
                BOOST_CHECK(!error);
                executed.set_value(std::move(header));
            });
        auto header = executed.get_future().get();
        BOOST_REQUIRE(header);

        for (size_t i = 0; i < c_txs.size(); ++i)
        {
            auto receipt = block->receipts()[i];
            recorder.add(config, c_txs[i].name + ".budget",
                {.status = 0, .gasUsed = executor->budget(static_cast<int64_t>(i)), .digest = ""});
            recorder.add(config, c_txs[i].name,
                {.status = receipt->status(),
                    .gasUsed = lowBits(receipt->gasUsed()),
                    .digest = ""});
        }
        recorder.add(
            config, "block", {.status = 0, .gasUsed = lowBits(header->gasUsed()), .digest = ""});
        blockExecutive.reset();
        scheduler.reset();
    }

    void runAll(bool serial, Recorder& recorder)
    {
        for (unsigned mask = 0; mask < (1U << c_flags.size()); ++mask)
        {
            ledger::Features features;
            std::string name = "none";
            for (size_t i = 0; i < c_flags.size(); ++i)
            {
                if ((mask & (1U << i)) != 0)
                {
                    features.set(c_flags[i].second);
                    name += "+" + std::string(c_flags[i].first);
                }
            }
            run(name, features, serial, recorder);
        }
    }
};

// clang-format off
const std::vector<Row> c_expectedDmc = {
        {"none", "plain.budget", 0, 3000000, ""},
        {"none", "plain", 0, 1000, ""},
        {"none", "system.budget", 0, 3000000000, ""},
        {"none", "system", 0, 0, ""},
        {"none", "systemDeploy.budget", 0, 3000000000, ""},
        {"none", "systemDeploy", 0, 0, ""},
        {"none", "create.budget", 0, 3000000, ""},
        {"none", "create", 0, 1000, ""},
        {"none", "capped.budget", 0, 3000000, ""},
        {"none", "capped", 0, 1000, ""},
        {"none", "block", 0, 3000, ""},
        {"none+gascalc", "plain.budget", 0, 3000000, ""},
        {"none+gascalc", "plain", 0, 1000, ""},
        {"none+gascalc", "system.budget", 0, 3000000, ""},
        {"none+gascalc", "system", 0, 0, ""},
        {"none+gascalc", "systemDeploy.budget", 0, 3000000, ""},
        {"none+gascalc", "systemDeploy", 0, 0, ""},
        {"none+gascalc", "create.budget", 0, 3000000, ""},
        {"none+gascalc", "create", 0, 1000, ""},
        {"none+gascalc", "capped.budget", 0, 3000000, ""},
        {"none+gascalc", "capped", 0, 1000, ""},
        {"none+gascalc", "block", 0, 3000, ""},
        {"none+gasused", "plain.budget", 0, 3000000, ""},
        {"none+gasused", "plain", 0, 1000, ""},
        {"none+gasused", "system.budget", 0, 3000000000, ""},
        {"none+gasused", "system", 0, -2996999000, ""},
        {"none+gasused", "systemDeploy.budget", 0, 3000000000, ""},
        {"none+gasused", "systemDeploy", 0, -2996999000, ""},
        {"none+gasused", "create.budget", 0, 3000000, ""},
        {"none+gasused", "create", 0, 1000, ""},
        {"none+gasused", "capped.budget", 0, 3000000, ""},
        {"none+gasused", "capped", 0, 1000, ""},
        {"none+gasused", "block", 0, -5993995000, ""},
        {"none+gascalc+gasused", "plain.budget", 0, 3000000, ""},
        {"none+gascalc+gasused", "plain", 0, 1000, ""},
        {"none+gascalc+gasused", "system.budget", 0, 3000000, ""},
        {"none+gascalc+gasused", "system", 0, 1000, ""},
        {"none+gascalc+gasused", "systemDeploy.budget", 0, 3000000, ""},
        {"none+gascalc+gasused", "systemDeploy", 0, 1000, ""},
        {"none+gascalc+gasused", "create.budget", 0, 3000000, ""},
        {"none+gascalc+gasused", "create", 0, 1000, ""},
        {"none+gascalc+gasused", "capped.budget", 0, 3000000, ""},
        {"none+gascalc+gasused", "capped", 0, 1000, ""},
        {"none+gascalc+gasused", "block", 0, 5000, ""},
        {"none+dmcDeploy", "plain.budget", 0, 3000000, ""},
        {"none+dmcDeploy", "plain", 0, 1000, ""},
        {"none+dmcDeploy", "system.budget", 0, 3000000000, ""},
        {"none+dmcDeploy", "system", 0, 0, ""},
        {"none+dmcDeploy", "systemDeploy.budget", 0, 3000000000, ""},
        {"none+dmcDeploy", "systemDeploy", 0, -2996999000, ""},
        {"none+dmcDeploy", "create.budget", 0, 3000000, ""},
        {"none+dmcDeploy", "create", 0, 1000, ""},
        {"none+dmcDeploy", "capped.budget", 0, 3000000, ""},
        {"none+dmcDeploy", "capped", 0, 1000, ""},
        {"none+dmcDeploy", "block", 0, -2996996000, ""},
        {"none+gascalc+dmcDeploy", "plain.budget", 0, 3000000, ""},
        {"none+gascalc+dmcDeploy", "plain", 0, 1000, ""},
        {"none+gascalc+dmcDeploy", "system.budget", 0, 3000000, ""},
        {"none+gascalc+dmcDeploy", "system", 0, 0, ""},
        {"none+gascalc+dmcDeploy", "systemDeploy.budget", 0, 3000000, ""},
        {"none+gascalc+dmcDeploy", "systemDeploy", 0, 1000, ""},
        {"none+gascalc+dmcDeploy", "create.budget", 0, 3000000, ""},
        {"none+gascalc+dmcDeploy", "create", 0, 1000, ""},
        {"none+gascalc+dmcDeploy", "capped.budget", 0, 3000000, ""},
        {"none+gascalc+dmcDeploy", "capped", 0, 1000, ""},
        {"none+gascalc+dmcDeploy", "block", 0, 4000, ""},
        {"none+gasused+dmcDeploy", "plain.budget", 0, 3000000, ""},
        {"none+gasused+dmcDeploy", "plain", 0, 1000, ""},
        {"none+gasused+dmcDeploy", "system.budget", 0, 3000000000, ""},
        {"none+gasused+dmcDeploy", "system", 0, -2996999000, ""},
        {"none+gasused+dmcDeploy", "systemDeploy.budget", 0, 3000000000, ""},
        {"none+gasused+dmcDeploy", "systemDeploy", 0, -2996999000, ""},
        {"none+gasused+dmcDeploy", "create.budget", 0, 3000000, ""},
        {"none+gasused+dmcDeploy", "create", 0, 1000, ""},
        {"none+gasused+dmcDeploy", "capped.budget", 0, 3000000, ""},
        {"none+gasused+dmcDeploy", "capped", 0, 1000, ""},
        {"none+gasused+dmcDeploy", "block", 0, -5993995000, ""},
        {"none+gascalc+gasused+dmcDeploy", "plain.budget", 0, 3000000, ""},
        {"none+gascalc+gasused+dmcDeploy", "plain", 0, 1000, ""},
        {"none+gascalc+gasused+dmcDeploy", "system.budget", 0, 3000000, ""},
        {"none+gascalc+gasused+dmcDeploy", "system", 0, 1000, ""},
        {"none+gascalc+gasused+dmcDeploy", "systemDeploy.budget", 0, 3000000, ""},
        {"none+gascalc+gasused+dmcDeploy", "systemDeploy", 0, 1000, ""},
        {"none+gascalc+gasused+dmcDeploy", "create.budget", 0, 3000000, ""},
        {"none+gascalc+gasused+dmcDeploy", "create", 0, 1000, ""},
        {"none+gascalc+gasused+dmcDeploy", "capped.budget", 0, 3000000, ""},
        {"none+gascalc+gasused+dmcDeploy", "capped", 0, 1000, ""},
        {"none+gascalc+gasused+dmcDeploy", "block", 0, 5000, ""},
};
// clang-format on

// clang-format off
const std::vector<Row> c_expectedSerial = {
        {"none", "plain.budget", 0, 3000000, ""},
        {"none", "plain", 0, 1000, ""},
        {"none", "system.budget", 0, 3000000000, ""},
        {"none", "system", 0, 0, ""},
        {"none", "systemDeploy.budget", 0, 3000000000, ""},
        {"none", "systemDeploy", 0, 0, ""},
        {"none", "create.budget", 0, 3000000, ""},
        {"none", "create", 0, 1000, ""},
        {"none", "capped.budget", 0, 3000000, ""},
        {"none", "capped", 0, 1000, ""},
        {"none", "block", 0, 3000, ""},
        {"none+gascalc", "plain.budget", 0, 3000000, ""},
        {"none+gascalc", "plain", 0, 1000, ""},
        {"none+gascalc", "system.budget", 0, 3000000, ""},
        {"none+gascalc", "system", 0, 0, ""},
        {"none+gascalc", "systemDeploy.budget", 0, 3000000, ""},
        {"none+gascalc", "systemDeploy", 0, 0, ""},
        {"none+gascalc", "create.budget", 0, 3000000, ""},
        {"none+gascalc", "create", 0, 1000, ""},
        {"none+gascalc", "capped.budget", 0, 3000000, ""},
        {"none+gascalc", "capped", 0, 1000, ""},
        {"none+gascalc", "block", 0, 3000, ""},
        {"none+gasused", "plain.budget", 0, 3000000, ""},
        {"none+gasused", "plain", 0, 1000, ""},
        {"none+gasused", "system.budget", 0, 3000000000, ""},
        {"none+gasused", "system", 0, -2996999000, ""},
        {"none+gasused", "systemDeploy.budget", 0, 3000000000, ""},
        {"none+gasused", "systemDeploy", 0, -2996999000, ""},
        {"none+gasused", "create.budget", 0, 3000000, ""},
        {"none+gasused", "create", 0, 1000, ""},
        {"none+gasused", "capped.budget", 0, 3000000, ""},
        {"none+gasused", "capped", 0, 1000, ""},
        {"none+gasused", "block", 0, -5993995000, ""},
        {"none+gascalc+gasused", "plain.budget", 0, 3000000, ""},
        {"none+gascalc+gasused", "plain", 0, 1000, ""},
        {"none+gascalc+gasused", "system.budget", 0, 3000000, ""},
        {"none+gascalc+gasused", "system", 0, 1000, ""},
        {"none+gascalc+gasused", "systemDeploy.budget", 0, 3000000, ""},
        {"none+gascalc+gasused", "systemDeploy", 0, 1000, ""},
        {"none+gascalc+gasused", "create.budget", 0, 3000000, ""},
        {"none+gascalc+gasused", "create", 0, 1000, ""},
        {"none+gascalc+gasused", "capped.budget", 0, 3000000, ""},
        {"none+gascalc+gasused", "capped", 0, 1000, ""},
        {"none+gascalc+gasused", "block", 0, 5000, ""},
        {"none+dmcDeploy", "plain.budget", 0, 3000000, ""},
        {"none+dmcDeploy", "plain", 0, 1000, ""},
        {"none+dmcDeploy", "system.budget", 0, 3000000000, ""},
        {"none+dmcDeploy", "system", 0, 0, ""},
        {"none+dmcDeploy", "systemDeploy.budget", 0, 3000000000, ""},
        {"none+dmcDeploy", "systemDeploy", 0, -2996999000, ""},
        {"none+dmcDeploy", "create.budget", 0, 3000000, ""},
        {"none+dmcDeploy", "create", 0, 1000, ""},
        {"none+dmcDeploy", "capped.budget", 0, 3000000, ""},
        {"none+dmcDeploy", "capped", 0, 1000, ""},
        {"none+dmcDeploy", "block", 0, -2996996000, ""},
        {"none+gascalc+dmcDeploy", "plain.budget", 0, 3000000, ""},
        {"none+gascalc+dmcDeploy", "plain", 0, 1000, ""},
        {"none+gascalc+dmcDeploy", "system.budget", 0, 3000000, ""},
        {"none+gascalc+dmcDeploy", "system", 0, 0, ""},
        {"none+gascalc+dmcDeploy", "systemDeploy.budget", 0, 3000000, ""},
        {"none+gascalc+dmcDeploy", "systemDeploy", 0, 1000, ""},
        {"none+gascalc+dmcDeploy", "create.budget", 0, 3000000, ""},
        {"none+gascalc+dmcDeploy", "create", 0, 1000, ""},
        {"none+gascalc+dmcDeploy", "capped.budget", 0, 3000000, ""},
        {"none+gascalc+dmcDeploy", "capped", 0, 1000, ""},
        {"none+gascalc+dmcDeploy", "block", 0, 4000, ""},
        {"none+gasused+dmcDeploy", "plain.budget", 0, 3000000, ""},
        {"none+gasused+dmcDeploy", "plain", 0, 1000, ""},
        {"none+gasused+dmcDeploy", "system.budget", 0, 3000000000, ""},
        {"none+gasused+dmcDeploy", "system", 0, -2996999000, ""},
        {"none+gasused+dmcDeploy", "systemDeploy.budget", 0, 3000000000, ""},
        {"none+gasused+dmcDeploy", "systemDeploy", 0, -2996999000, ""},
        {"none+gasused+dmcDeploy", "create.budget", 0, 3000000, ""},
        {"none+gasused+dmcDeploy", "create", 0, 1000, ""},
        {"none+gasused+dmcDeploy", "capped.budget", 0, 3000000, ""},
        {"none+gasused+dmcDeploy", "capped", 0, 1000, ""},
        {"none+gasused+dmcDeploy", "block", 0, -5993995000, ""},
        {"none+gascalc+gasused+dmcDeploy", "plain.budget", 0, 3000000, ""},
        {"none+gascalc+gasused+dmcDeploy", "plain", 0, 1000, ""},
        {"none+gascalc+gasused+dmcDeploy", "system.budget", 0, 3000000, ""},
        {"none+gascalc+gasused+dmcDeploy", "system", 0, 1000, ""},
        {"none+gascalc+gasused+dmcDeploy", "systemDeploy.budget", 0, 3000000, ""},
        {"none+gascalc+gasused+dmcDeploy", "systemDeploy", 0, 1000, ""},
        {"none+gascalc+gasused+dmcDeploy", "create.budget", 0, 3000000, ""},
        {"none+gascalc+gasused+dmcDeploy", "create", 0, 1000, ""},
        {"none+gascalc+gasused+dmcDeploy", "capped.budget", 0, 3000000, ""},
        {"none+gascalc+gasused+dmcDeploy", "capped", 0, 1000, ""},
        {"none+gascalc+gasused+dmcDeploy", "block", 0, 5000, ""},
};
// clang-format on
}  // namespace

BOOST_FIXTURE_TEST_SUITE(GasCharacterization_SchedulerTest, GasCharacterizationSchedulerFixture)

BOOST_AUTO_TEST_CASE(dmcBlockUnderEachConfiguration)
{
    Recorder recorder;
    runAll(false, recorder);
    recorder.check(c_expectedDmc);
}

BOOST_AUTO_TEST_CASE(serialBlockUnderEachConfiguration)
{
    Recorder recorder;
    runAll(true, recorder);
    recorder.check(c_expectedSerial);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
