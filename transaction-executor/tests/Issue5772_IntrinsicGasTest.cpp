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
 * @brief Issue #5772 on executor_version 1: bugfix_tx_intrinsic_gas charges Ethereum's intrinsic
 *        gas before the top-level frame and 200 gas per byte of deployed code.
 *
 * The expected numbers are worked out by hand from the Yellow Paper constants, not read back
 * from TxGasModel.h, so a change to the shared model shows up here.
 */
#include "../bcos-transaction-executor/TransactionExecutorImpl.h"
#include "TestMemoryStorage.h"
#include "bcos-executor/src/Common.h"
#include "bcos-framework/ledger/EVMAccount.h"
#include "bcos-framework/ledger/Features.h"
#include "bcos-framework/protocol/Protocol.h"
#include <bcos-codec/rlp/RLPDecode.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-rlp-protocol/Web3Transaction.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionFactoryImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <boost/algorithm/hex.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::executor_v1;

namespace
{
// PUSH1 0x2a PUSH1 0 SSTORE | PUSH1 10 PUSH1 0x11 PUSH1 0 CODECOPY | PUSH1 10 PUSH1 0 RETURN,
// then 10 bytes of runtime code that begins with STOP. 27 bytes: 8 zero, 19 non-zero.
constexpr std::string_view c_initCode = "602a600055600a6011600039600a6000f300000000000101010101";
constexpr int64_t c_runtimeSize = 10;
// 21000 base + 32000 creation + (8 * 4 + 19 * 16) calldata + 2 * 1 init code word (the lane
// never runs below Cancun, so EIP-3860 applies)
constexpr int64_t c_createIntrinsic = 21000 + 32000 + 336 + 2;
constexpr int64_t c_oldDepositPerByte = 20;
constexpr int64_t c_newDepositPerByte = 200;

struct Issue5772Fixture
{
    std::shared_ptr<bcos::crypto::CryptoSuite> cryptoSuite =
        std::make_shared<bcos::crypto::CryptoSuite>(
            std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
    bcostars::protocol::TransactionFactoryImpl transactionFactory{cryptoSuite};
    bcostars::protocol::TransactionReceiptFactoryImpl receiptFactory{cryptoSuite};
    PrecompiledManager precompiledManager{cryptoSuite->hashImpl()};
    TransactionExecutorImpl executor{receiptFactory, cryptoSuite->hashImpl(), precompiledManager};
    bcostars::protocol::BlockHeaderImpl blockHeader;
    evmc_address sender = bcos::unhexAddress("abcdef0123456789abcdef0123456789abcdef01");

    Issue5772Fixture()
    {
        bcos::executor::GlobalHashImpl::g_hashImpl = std::make_shared<bcos::crypto::Keccak256>();
        blockHeader.setVersion(static_cast<uint32_t>(protocol::BlockVersion::V3_17_0_VERSION));
        // A block number no other suite uses: a contract address derives from (block, context,
        // seq), and the process-wide executable cache is keyed by address, so a contract
        // deployed here at another suite's address would be served to that suite's calls.
        blockHeader.setNumber(5772001);
        blockHeader.calculateHash(*cryptoSuite->hashImpl());
    }

    // every fix through 3.17.0, plus the flag under test when asked
    static ledger::LedgerConfig config(bool intrinsicGas, bool prague = false)
    {
        ledger::Features features;
        features.setGenesisFeatures(protocol::BlockVersion::V3_17_0_VERSION);
        if (prague)
        {
            features.set(ledger::Features::Flag::feature_evm_cancun);
            features.set(ledger::Features::Flag::feature_evm_prague);
        }
        if (intrinsicGas)
        {
            features.set(ledger::Features::Flag::bugfix_tx_intrinsic_gas);
        }
        ledger::LedgerConfig ledgerConfig;
        ledgerConfig.setFeatures(features);
        return ledgerConfig;
    }

    // One chain per lane setting: a deployment at context 0, then calls against it.
    struct Chain
    {
        Issue5772Fixture& fixture;
        ledger::LedgerConfig ledgerConfig;
        MutableStorage storage;
        int contextID = 0;

        protocol::TransactionReceipt::Ptr run(std::string_view to, bcos::bytes input)
        {
            // version 0: the factory forces gasLimit to 0, so the budget is the chain's
            // tx_gas_limit (LedgerConfig::gasLimit)
            auto tx = fixture.transactionFactory.createTransaction(
                0, std::string(to), std::move(input), std::to_string(contextID), 0, "", "", 0);
            tx->forceSender(bytes(fixture.sender.bytes, fixture.sender.bytes + 20));
            return task::syncWait(fixture.executor.executeTransaction(
                storage, fixture.blockHeader, *tx, contextID++, ledgerConfig, false));
        }

        protocol::TransactionReceipt::Ptr deploy()
        {
            bcos::bytes code;
            boost::algorithm::unhex(c_initCode, std::back_inserter(code));
            return run("", std::move(code));
        }
    };

    Chain chain(bool intrinsicGas, bool prague = false)
    {
        return Chain{.fixture = *this, .ledgerConfig = config(intrinsicGas, prague), .storage = {}};
    }

    static int64_t gasUsed(protocol::TransactionReceipt const& receipt)
    {
        return static_cast<int64_t>(receipt.gasUsed());
    }
    static constexpr auto c_ok = static_cast<int32_t>(protocol::TransactionStatus::None);
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(Issue5772_IntrinsicGasTest, Issue5772Fixture)

// The failing scenario of the issue: a deployment that costs less than a transfer.
BOOST_AUTO_TEST_CASE(deploymentPaysIntrinsicGasAndEthereumCodeDeposit)
{
    auto off = chain(false);
    auto on = chain(true);
    auto offReceipt = off.deploy();
    auto onReceipt = on.deploy();
    BOOST_REQUIRE_EQUAL(offReceipt->status(), c_ok);
    BOOST_REQUIRE_EQUAL(onReceipt->status(), c_ok);
    BOOST_REQUIRE(!onReceipt->contractAddress().empty());

    // flag off the deployment pays for nothing but the EVM run and 20 gas per byte (on a chain
    // without bugfix_evm_storage_status that is the 7589 gas of the issue); flag on it pays
    // the same run plus the intrinsic gas and the 180 gas per byte the deposit grew by
    BOOST_CHECK_EQUAL(gasUsed(*onReceipt) - gasUsed(*offReceipt),
        c_createIntrinsic + c_runtimeSize * (c_newDepositPerByte - c_oldDepositPerByte));
    BOOST_CHECK_GT(gasUsed(*onReceipt), 21000);
}

// A top-level call already paid 21000 on this lane; the flag adds its calldata and nothing else.
BOOST_AUTO_TEST_CASE(callPaysBaseOnceAndCalldata)
{
    for (bool flag : {false, true})
    {
        auto lane = chain(flag);
        auto deployed = lane.deploy();
        BOOST_REQUIRE_EQUAL(deployed->status(), c_ok);
        // the runtime code is a STOP, so the frame itself costs nothing
        auto receipt = lane.run(deployed->contractAddress(), bcos::bytes{1, 2, 0, 0});
        BOOST_REQUIRE_EQUAL(receipt->status(), c_ok);
        BOOST_CHECK_EQUAL(gasUsed(*receipt), flag ? 21000 + (2 * 16 + 2 * 4) : 21000);

        // an empty call to an address without code is the lane's plain transfer shape
        auto plain = lane.run("00000000000000000000000000000000000000ee", {});
        BOOST_REQUIRE_EQUAL(plain->status(), c_ok);
        BOOST_CHECK_EQUAL(gasUsed(*plain), 21000);
    }
}

// A budget below the intrinsic gas: the EVM does not run, the whole budget is consumed.
BOOST_AUTO_TEST_CASE(budgetBelowIntrinsicGasFailsWithoutExecuting)
{
    constexpr int64_t budget = c_createIntrinsic - 1;
    auto lane = chain(true);
    lane.ledgerConfig.setGasLimit({static_cast<uint64_t>(budget), 0});
    auto receipt = lane.deploy();
    BOOST_CHECK_EQUAL(
        receipt->status(), static_cast<int32_t>(protocol::TransactionStatus::OutOfGas));
    BOOST_CHECK_EQUAL(gasUsed(*receipt), budget);
    BOOST_CHECK(receipt->contractAddress().empty());

    // one more unit of gas reaches the EVM, which then runs out inside the init code
    auto enough = chain(true);
    enough.ledgerConfig.setGasLimit({static_cast<uint64_t>(c_createIntrinsic), 0});
    auto ran = enough.deploy();
    BOOST_CHECK_NE(ran->status(), c_ok);
    BOOST_CHECK_EQUAL(gasUsed(*ran), c_createIntrinsic);
}

// From Prague on this lane already took max(calldata, EIP-7623 floor) up front. The flag keeps
// that total: 100 non-zero bytes are 400 tokens, so the floor (4000) beats the calldata (1600).
BOOST_AUTO_TEST_CASE(pragueCalldataFloorIsUnchanged)
{
    for (bool flag : {false, true})
    {
        auto lane = chain(flag, true);
        auto deployed = lane.deploy();
        BOOST_REQUIRE_EQUAL(deployed->status(), c_ok);
        auto receipt = lane.run(deployed->contractAddress(), bcos::bytes(100, 0xff));
        BOOST_REQUIRE_EQUAL(receipt->status(), c_ok);
        BOOST_CHECK_EQUAL(gasUsed(*receipt), 21000 + 4000);
    }
}

// The access list is part of the intrinsic gas: 2400 per address, 1900 per storage key. The
// transaction is the EIP-2930 fixture of Web3AccessListResolverTest: 16 non-zero bytes of
// calldata, two addresses, two storage keys. Its recipient has no code, so the frame fails
// before running anything and the receipt shows the upfront charge alone.
BOOST_AUTO_TEST_CASE(accessListIsPricedPerAddressAndKey)
{
    constexpr std::string_view rawTx =
        "0x01f8f205078506fc23ac008357b58494811a752c8cd697e3cb27279c330ed1ada745a8d7881bc16d674ec800"
        "00906ebaf477f83e051589c1188bcc6ddccdf872f85994de0b295669a9fd93d5f28d9ec85e40f4cb697baef842"
        "a00000000000000000000000000000000000000000000000000000000000000003a00000000000000000000000"
        "000000000000000000000000000000000000000007d694bb9bc244d798123fde783fcc1c72d3bb8c189413c080"
        "a036b241b061a36a32ab7fe86c7aa9eb592dd59018cd0443adc0903590c16b02b0a05edcc541b4741c5cc6dd34"
        "7c5ed9577ef293a62787b4510465fadbfe39ee4094";
    // What the signature covers is the envelope, so the Tars mirror of the list can be absent:
    // kept, stripped with the kind still set (resolver path B), or stripped along with the kind
    // (path C). The same signed transaction pays the same gas in all three.
    enum class Mirror
    {
        kept,
        listStripped,
        listAndKindStripped
    };
    for (auto [flag, mirror] : {std::pair{false, Mirror::kept}, std::pair{true, Mirror::kept},
             std::pair{true, Mirror::listStripped}, std::pair{true, Mirror::listAndKindStripped}})
    {
        BOOST_TEST_INFO_SCOPE("flag " << flag << " mirror " << static_cast<int>(mirror));
        auto raw = fromHexWithPrefix(rawTx);
        auto rawRef = ref(raw);
        bcos::rpc::Web3Transaction web3Tx{};
        BOOST_REQUIRE_NO_THROW(bcos::codec::rlp::decode(rawRef, web3Tx));
        auto inner = std::make_shared<bcostars::Transaction>(web3Tx.takeToTarsTransaction());
        auto const txHash = web3Tx.txHash();
        inner->extraTransactionHash.assign(txHash.begin(), txHash.end());
        BOOST_REQUIRE_EQUAL(inner->data.accessList.size(), 2U);
        if (mirror != Mirror::kept)
        {
            inner->data.accessList.clear();
        }
        if (mirror == Mirror::listAndKindStripped)
        {
            inner->web3TypedTxKind = 0;
        }
        bcostars::protocol::TransactionImpl tx([inner]() { return inner.get(); });
        tx.forceSender(bytes(sender.bytes, sender.bytes + 20));

        auto lane = chain(flag);
        protocol::TransactionReceipt::Ptr receipt;
        try
        {
            receipt = task::syncWait(executor.executeTransaction(
                lane.storage, blockHeader, tx, 0, lane.ledgerConfig, false));
        }
        catch (...)
        {
            BOOST_FAIL(boost::current_exception_diagnostic_information());
        }
        BOOST_CHECK_NE(receipt->status(), c_ok);
        BOOST_CHECK_EQUAL(gasUsed(*receipt), flag ? 21000 + 16 * 16 + 2 * 2400 + 2 * 1900 : 21000);
    }
}

// A budget that runs the init code but cannot pay the deposit fails with the whole budget gone
// and nothing deployed. The cost is measured, then the budget is set one unit either side of it.
BOOST_AUTO_TEST_CASE(depositTheBudgetCannotPayBurnsTheBudget)
{
    auto measured = chain(true);
    auto measuredReceipt = measured.deploy();
    BOOST_REQUIRE_EQUAL(measuredReceipt->status(), c_ok);
    const auto cost = gasUsed(*measuredReceipt);

    auto exact = chain(true);
    exact.ledgerConfig.setGasLimit({static_cast<uint64_t>(cost), 0});
    auto exactReceipt = exact.deploy();
    BOOST_CHECK_EQUAL(exactReceipt->status(), c_ok);
    BOOST_CHECK_EQUAL(gasUsed(*exactReceipt), cost);

    auto short1 = chain(true);
    short1.ledgerConfig.setGasLimit({static_cast<uint64_t>(cost - 1), 0});
    auto shortReceipt = short1.deploy();
    BOOST_CHECK_EQUAL(
        shortReceipt->status(), static_cast<int32_t>(protocol::TransactionStatus::OutOfGas));
    BOOST_CHECK_EQUAL(gasUsed(*shortReceipt), cost - 1);
    BOOST_CHECK(shortReceipt->contractAddress().empty());
}

// A sender who cannot buy the gas pays the intrinsic gas of this transaction, not a fixed 21000
// that a deployment's intrinsic gas left behind, and never more than the budget it signed for.
BOOST_AUTO_TEST_CASE(buyGasPenaltyIsTheTransactionsIntrinsicGas)
{
    const auto runDeploy = [this](bool flag, int64_t gasLimit, std::string value,
                               int64_t balance) -> std::pair<int64_t, u256> {
        return task::syncWait(
            [](Issue5772Fixture& self, bool flag, int64_t gasLimit, std::string value,
                int64_t balance) -> task::Task<std::pair<int64_t, u256>> {
                auto lane = self.chain(flag);
                auto features = lane.ledgerConfig.features();
                features.set(ledger::Features::Flag::feature_balance);
                features.set(ledger::Features::Flag::bugfix_gas_payment_balance_precheck);
                lane.ledgerConfig.setFeatures(features);
                lane.ledgerConfig.setGasPrice({"1", 0});

                bcos::bytes code;
                boost::algorithm::unhex(c_initCode, std::back_inserter(code));
                auto tx = self.transactionFactory.createTransaction(1, "", code, "0x5", 0, "", "",
                    0, std::string{}, std::move(value), "0x1", gasLimit, "0x0", "0x0");
                tx->forceSender(bytes(self.sender.bytes, self.sender.bytes + 20));
                dynamic_cast<bcostars::protocol::TransactionImpl&>(*tx).mutableInner().type = 1;

                ledger::account::EVMAccount account(
                    lane.storage, self.sender, ledger::account::AddressTableMode::Hex);
                co_await account.setBalance(u256(balance));
                auto receipt = co_await self.executor.executeTransaction(
                    lane.storage, self.blockHeader, *tx, 0, lane.ledgerConfig, false);
                BOOST_CHECK_EQUAL(receipt->status(),
                    static_cast<int32_t>(protocol::TransactionStatus::NotEnoughCash));
                co_return std::pair{gasUsed(*receipt), co_await account.balance()};
            }(*this, flag, gasLimit, std::move(value), balance));
    };

    // balance 60000 < budget 100000: flag off the fixed 21000, flag on the deployment's intrinsic
    {
        auto [used, balance] = runDeploy(false, 100000, "0x0", 60000);
        BOOST_CHECK_EQUAL(used, 21000);
        BOOST_CHECK_EQUAL(balance, u256(60000 - 21000));
    }
    {
        auto [used, balance] = runDeploy(true, 100000, "0x0", 60000);
        BOOST_CHECK_EQUAL(used, c_createIntrinsic);
        BOOST_CHECK_EQUAL(balance, u256(60000 - c_createIntrinsic));
    }
    // budget 30000 < intrinsic, and the value makes the balance fall short: capped at the budget
    {
        auto [used, balance] = runDeploy(true, 30000, "0x9c40", 60000);
        BOOST_CHECK_EQUAL(used, 30000);
        BOOST_CHECK_EQUAL(balance, u256(60000 - 30000));
    }
    // a balance below the penalty is drained, and gasUsed reports what was actually taken
    {
        auto [used, balance] = runDeploy(true, 100000, "0x0", 500);
        BOOST_CHECK_EQUAL(used, 500);
        BOOST_CHECK_EQUAL(balance, u256(0));
    }
}

BOOST_AUTO_TEST_SUITE_END()
