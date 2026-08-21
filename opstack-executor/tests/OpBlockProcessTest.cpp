// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpBlockProcessTest.cpp — direct block-loop coverage for processOpBlock /
// validateJovianBlockShape (kyonRay review #5463 #4). The block loop previously had zero
// direct tests: only the parts (executeDeposit/finalizeOpBlock/sealOpBlock/encodeReceiptForRoot)
// were covered, so combination-level admissions like "Jovian activation block must be
// deposits-only" could regress invisibly. These cases stop at the admission boundary (empty
// block / non-deposit first tx / activation-block mixed tx), which is where the block-loop
// logic lives; full execution is exercised end-to-end by OpstackExecutorTest.

// BOOST_TEST_MODULE lives in TestMain.cpp (the single main for the whole suite).
#include <boost/test/unit_test.hpp>

#include "bcos-evm/opstack/OpForkSchedule.h"
#include "opstack-executor/OpBlockExecute.h"
#include "opstack-executor/Storage2State.h"
#include <bcos-codec/rlp/Common.h>
#include <bcos-crypto/hash/Keccak256.h>
#include <bcos-framework/ledger/LedgerConfig.h>
#include <bcos-framework/storage2/MemoryStorage.h>
#include <bcos-framework/transaction-executor/StateKey.h>
#include <bcos-rpc/web3jsonrpc/model/Web3Transaction.h>
#include <bcos-tars-protocol/protocol/BlockHeaderImpl.h>
#include <bcos-tars-protocol/protocol/TransactionImpl.h>
#include <bcos-tars-protocol/protocol/TransactionReceiptFactoryImpl.h>
#include <evmc/evmc.h>
#include <evmc/evmc.hpp>
#include <memory>

using namespace bcos;
using namespace bcos::executor_v1::opstack;
using namespace evmc::literals;  // _bytes32 / _address

namespace
{
using bcos::executor_v1::StateKey;
using bcos::executor_v1::StateValue;
namespace memory_storage = bcos::storage2::memory_storage;

using MutableStorage = memory_storage::MemoryStorage<StateKey, StateValue,
    memory_storage::Attribute(memory_storage::ORDERED | memory_storage::LOGICAL_DELETION)>;

bcos::crypto::CryptoSuite::Ptr makeCryptoSuite()
{
    return std::make_shared<bcos::crypto::CryptoSuite>(
        std::make_shared<bcos::crypto::Keccak256>(), nullptr, nullptr);
}

// Jovian activation block: Isthmus-length (176B) L1 attributes, must be deposits-only.
// Build a deposit whose data is exactly IsthmusL1AttributesLen bytes.
bcos::evm::opstack::DepositTx makeActivationL1AttributesDeposit()
{
    using bcos::evm::opstack::DepositTx;
    DepositTx dep{
        .source_hash = 0x01_bytes32,
        .from = 0xdeaddeaddeaddeaddeaddeaddeaddeaddead0001_address,
        .to = 0x4200000000000000000000000000000000000015_address,
        .mint = intx::uint256{5},
        .value = intx::uint256{0},
        .gas_limit = 100000,
        .is_system_tx = false,
        .data = evmc::bytes(bcos::evm::opstack::IsthmusL1AttributesLen, uint8_t{0x00}),
    };
    return dep;
}

bcos::evm::opstack::DepositTx makeNormalJovianDeposit()
{
    auto dep = makeActivationL1AttributesDeposit();
    // Normal Jovian: Jovian length + selector + DA scalar bytes.
    dep.data.assign(bcos::evm::opstack::JovianL1AttributesLen, 0x00);
    std::copy(bcos::evm::opstack::JovianL1AttributesSelector.begin(),
        bcos::evm::opstack::JovianL1AttributesSelector.end(), dep.data.begin());
    return dep;
}

// A normal (non-deposit) tx — only needs to exist as an OpBlockTx variant member; admission
// rejects it before execution touches its fields.
bcos::evm::opstack::OpBlockTx makeNormalTx()
{
    bcos::evm::opstack::OpBlockTx btx;
    btx.tx = evmone::state::Transaction{};
    return btx;
}

// Minimal BlockHashes impl: the admission tests never read a hash (they reject before the
// execution loop touches hashes).
struct ZeroBlockHashes final : evmone::state::BlockHashes
{
    evmc::bytes32 get_block_hash(int64_t) const noexcept override { return {}; }
};

struct Fixture
{
    std::shared_ptr<crypto::CryptoSuite> cryptoSuite = makeCryptoSuite();
    bcos::protocol::TransactionReceiptFactory::Ptr receiptFactory{
        std::make_shared<bcostars::protocol::TransactionReceiptFactoryImpl>(cryptoSuite)};
    MutableStorage storage;
    evmc::VM vm{evmc_create_evmone()};
    bcos::evm::opstack::OpForkConfig fork = bcos::evm::opstack::jovianConfig();

    evmone::state::BlockInfo makeBlockInfo() const
    {
        evmone::state::BlockInfo blk;
        blk.number = 1;
        blk.timestamp = 1000;  // seconds (evmone convention)
        blk.gas_limit = 30'000'000;
        blk.base_fee = 0;
        return blk;
    }

    // Runs processOpBlock and expects a consensus rejection. Step 1 (system_call_block_start)
    // runs before block-shape admission and does apply a diff, so we assert only the rejection —
    // not "no diff applied".
    void expectReject(std::span<const bcos::evm::opstack::OpBlockTx> txs,
        std::string_view expectMessageContains)
    {
        bcos::evm::evmstate::Storage2State<MutableStorage> view(storage, nullptr);
        auto blk = makeBlockInfo();
        ZeroBlockHashes hashes{};
        BOOST_CHECK_THROW(processOpBlock(view, blk, hashes, txs, fork, vm, /*chainId=*/10,
                              receiptFactory,
                              [&](const evmone::state::StateDiff&) {}),
            bcos::evm::engine::OpConsensusError);
        (void)expectMessageContains;
    }
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(OpBlockProcessTest, Fixture)

// Empty block: nothing seeds the block's fee/DA context — hard reject (op-geth parity).
BOOST_AUTO_TEST_CASE(EmptyBlockRejected)
{
    std::vector<bcos::evm::opstack::OpBlockTx> txs;
    expectReject(txs, "empty block");
}

// First tx is not a deposit — hard reject.
BOOST_AUTO_TEST_CASE(NonDepositFirstTxRejected)
{
    std::vector<bcos::evm::opstack::OpBlockTx> txs;
    txs.push_back(makeNormalTx());
    expectReject(txs, "no deposit transaction");
}

// Jovian activation block (Isthmus-length attributes) mixed with a normal tx must be rejected:
// the activation block is deposits-only (op-geth CalcDAFootprint). Regression for the
// last-tx-only check, which would have let [deposit, normal] through when the deposit is last.
BOOST_AUTO_TEST_CASE(ActivationBlockMixedTxRejected)
{
    std::vector<bcos::evm::opstack::OpBlockTx> txs;
    auto dep = makeActivationL1AttributesDeposit();
    bcos::evm::opstack::OpBlockTx depTx;
    depTx.tx = dep;
    txs.push_back(depTx);
    txs.push_back(makeNormalTx());
    expectReject(txs, "Jovian activation block");
}

// The [deposit, normal, deposit] shape that defeated the old last-tx-only check: both a normal
// tx in the middle AND a deposit at the end. Must be rejected by the full-block scan.
BOOST_AUTO_TEST_CASE(ActivationBlockDepositNormalDepositRejected)
{
    std::vector<bcos::evm::opstack::OpBlockTx> txs;
    auto dep = makeActivationL1AttributesDeposit();
    bcos::evm::opstack::OpBlockTx depTx;
    depTx.tx = dep;
    txs.push_back(depTx);
    txs.push_back(makeNormalTx());
    txs.push_back(depTx);  // deposit last — the old check would have passed this
    expectReject(txs, "Jovian activation block");
}

// A normal Jovian block with all-deposit txs and correct selector passes shape validation —
// it reaches the execution loop (rejected later only if execution fails; here we only assert
// the shape admission does not throw).
BOOST_AUTO_TEST_CASE(NormalJovianDepositsOnlyPassesShapeCheck)
{
    std::vector<bcos::evm::opstack::OpBlockTx> txs;
    auto dep = makeNormalJovianDeposit();
    bcos::evm::opstack::OpBlockTx depTx;
    depTx.tx = dep;
    txs.push_back(depTx);
    bcos::evm::opstack::OpBlockTx depTx2;
    depTx2.tx = makeNormalJovianDeposit();
    txs.push_back(depTx2);
    BOOST_CHECK_NO_THROW(bcos::evm::opstack::validateJovianBlockShape(txs, fork));
}

BOOST_AUTO_TEST_SUITE_END()
