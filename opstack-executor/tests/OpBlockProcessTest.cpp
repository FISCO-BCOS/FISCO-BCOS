// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpBlockProcessTest.cpp — direct coverage for the processOpBlock admission boundary (empty
// block / non-deposit first tx / Jovian activation-block shape), which per-part tests alone
// cannot reach, plus one deposit-only happy path through the block loop. OpstackExecutorTest
// drives the per-tx scheduler path (executeTransaction/executeDeposit); it does NOT call
// processOpBlock, so this file's happy path is the only full-loop coverage.

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

// A normal (non-deposit) tx — admission rejects it before execution touches its fields.
bcos::evm::opstack::OpBlockTx makeNormalTx()
{
    bcos::evm::opstack::OpBlockTx btx;
    btx.tx = evmone::state::Transaction{};
    return btx;
}

// Admission tests reject before hashes are read.
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

    // Runs processOpBlock and expects a consensus rejection with the given message fragment.
    // Step 1 (system_call_block_start) runs before shape admission and applies a diff, so only
    // the rejection is asserted. The message pin distinguishes the shape-level rejection from a
    // later execution-loop throw (e.g. an invalid non-deposit tx would also throw
    // OpConsensusError).
    void expectReject(
        std::span<const bcos::evm::opstack::OpBlockTx> txs, std::string_view expectMessageContains)
    {
        bcos::evm::evmstate::Storage2State<MutableStorage> view(storage, nullptr);
        auto blk = makeBlockInfo();
        ZeroBlockHashes hashes{};
        BOOST_CHECK_EXCEPTION(processOpBlock(view, blk, hashes, txs, fork, vm, /*chainId=*/10,
                                  receiptFactory, [&](const evmone::state::StateDiff&) {}),
            bcos::evm::engine::OpConsensusError, [&](auto const& e) {
                return std::string(e.what()).find(expectMessageContains) != std::string::npos;
            });
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
// activation blocks are deposits-only.
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

// Jovian activation block with interleaved non-deposit but deposit last:
// op-geth checks only the last tx, so this passes shape validation (last is deposit).
BOOST_AUTO_TEST_CASE(ActivationBlockDepositNormalDepositLastPassesShape)
{
    // Jovian activation block with interleaved non-deposit but deposit last:
    // op-geth checks only the last tx, so SHAPE validation passes. The block is still
    // rejected (here by opValidate rejecting the under-specified test tx; the
    // deposit-after-non-deposit ordering rule is a WARNING-only demotion, not a hard gate) —
    // the point is that the rejection must NOT carry the Jovian shape message (anchored via
    // ActivationBlockMixedTxRejected, which does expect it when a normal tx is present and
    // the LAST tx is normal).
    std::vector<bcos::evm::opstack::OpBlockTx> txs;
    auto dep = makeActivationL1AttributesDeposit();
    bcos::evm::opstack::OpBlockTx depTx;
    depTx.tx = dep;
    txs.push_back(depTx);
    txs.push_back(makeNormalTx());
    txs.push_back(depTx);  // deposit last — op-geth parity: last-tx-only check passes
    bcos::evm::evmstate::Storage2State<MutableStorage> view(storage, nullptr);
    auto blk = makeBlockInfo();
    ZeroBlockHashes hashes{};
    BOOST_CHECK_EXCEPTION(processOpBlock(view, blk, hashes, txs, fork, vm, /*chainId=*/10,
                              receiptFactory, [&](const evmone::state::StateDiff&) {}),
        bcos::evm::engine::OpConsensusError, [](auto const& e) {
            return std::string(e.what()).find("Jovian activation block") == std::string::npos;
        });
}

// A normal Jovian all-deposit block with the correct selector passes shape validation.
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

// A typed envelope whose chainId field is over-wide (9 bytes) yields nullopt from
// web3ChainIdFromEnvelope — it must NOT be exempted as pre-EIP-155 legacy here (same
// typed+nullopt guard as m_prepare / TxValidator / EthEndpoint). Deposit first so the block
// reaches the execution loop's normal-tx chainId gate.
BOOST_AUTO_TEST_CASE(TypedEnvelopeUnparseableChainIdRejected)
{
    std::vector<bcos::evm::opstack::OpBlockTx> txs;
    bcos::evm::opstack::OpBlockTx depTx;
    depTx.tx = makeNormalJovianDeposit();
    txs.push_back(depTx);
    auto btx = makeNormalTx();
    // 0x02 (EIP-1559 marker) || rlp-list([0x89 || 9-byte chainId]).
    btx.signedEnvelope = {0x02, 0xca, 0x89, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09};
    txs.push_back(btx);
    expectReject(txs, "missing a parseable chainId");
}

// Happy path for the block loop: a Jovian all-deposit block drives processOpBlock end-to-end
// (system call → deposit txs → finalize) with a real Storage2State-backed applyDiff write-back.
// Guards the loop's deposit branch (runDeposit + write-back + cumulative-gas formatting + seal
// inputs) — the admission-reject cases above never reach it.
BOOST_AUTO_TEST_CASE(DepositOnlyBlockExecutesEndToEnd)
{
    Fixture f;
    std::vector<bcos::evm::opstack::OpBlockTx> txs;
    auto dep = makeNormalJovianDeposit();
    bcos::evm::opstack::OpBlockTx depTx;
    depTx.tx = dep;
    txs.push_back(depTx);
    bcos::evm::opstack::OpBlockTx depTx2;
    depTx2.tx = makeNormalJovianDeposit();
    txs.push_back(depTx2);

    auto blk = f.makeBlockInfo();
    ZeroBlockHashes hashes{};
    bcos::evm::evmstate::Storage2State<MutableStorage> view(f.storage, nullptr);
    auto result = bcos::evm::opstack::processOpBlock(view, blk, hashes, txs, f.fork, f.vm,
        /*chainId=*/10, f.receiptFactory,
        [&view](const evmone::state::StateDiff& diff) { view.applyDiff(diff); });
    // Two deposits executed: receipts with non-zero gas, cumulative gas monotonic, no
    // double-spend / crash.
    BOOST_REQUIRE_EQUAL(result.receipts.size(), 2u);
    BOOST_REQUIRE_EQUAL(result.txTypes.size(), 2u);
    BOOST_CHECK(result.txTypes[0] == static_cast<uint8_t>(bcos::evm::opstack::kDepositTxType));
    BOOST_CHECK_GT(result.gasUsed, 0);
    BOOST_CHECK_GE(result.receipts[1]->gasUsed(), result.receipts[0]->gasUsed());
}

BOOST_AUTO_TEST_SUITE_END()
