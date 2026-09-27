// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpEthDualRunTest — C1 golden suite for the bcos-evm-free OP executor. For each op-geth t8n
// corpus vector, one seeded pre-state is executed through the monolithic driver:
//   executeOpEthBlock → opEthMessagePasserStorage → sealOpEthBlock → computeOpEthTxRoot +
//   computeMptStateRoot (full rebuild from the empty root — the corpus blocks sit on a
//   genesis-less seeded state).
// The outcome is asserted against the vector's `_op_expected` block (op-geth's own
// expectations): header commitments (gasUsed/receiptsRoot/logsBloom/withdrawalsRoot/
// requestsHash/blobGasUsed/stateRoot), per-receipt fields + `_op_*` meta, and the postState
// balances/nonces (support/GoldenExpect.h). The golden txsRoot comes from the decoded golden
// RLP header (not carried by `_op_expected`); the golden blockHash itself is not recomputed
// (header RLP encoding is out of scope here).
//
// Step 3.3 retired the old-leg dual-run (the legacy bcos-evm baseline) — the golden IS the
// baseline now. The fixture/drivers live in support/DualRunHarness.h (shared with
// OpEthForkMatrixTest.cpp).

#include "support/DualRunHarness.h"
#include "support/GoldenExpect.h"
#include "support/GoldenSample.h"  // w6test loaders (pulls support/SeedPreState.h)

#include <bcos-utilities/DataConvertUtility.h>
#include <boost/test/unit_test.hpp>

#include <cstdint>
#include <vector>

using namespace opstack_test;

namespace
{

void goldenRunVector(std::string const& id)
{
    BOOST_TEST_CONTEXT("vector " << id)
    {
        auto const sample = w6test::loadVectorSample(id);
        auto const header = w6test::decodeGoldenHeader(sample);
        std::vector<bcos::bytes> rawTxBytes;
        rawTxBytes.reserve(sample.golden["rawTransactions"].size());
        for (auto const& raw : sample.golden["rawTransactions"])
            rawTxBytes.push_back(bcos::fromHex(raw.asString()));
        BOOST_REQUIRE(!rawTxBytes.empty());
        for (auto const& env : rawTxBytes)
            BOOST_REQUIRE(!env.empty());

        DualRunFixture f;
        std::vector<bcos::protocol::Transaction::ConstPtr> transactions;
        transactions.reserve(rawTxBytes.size());
        for (auto const& env : rawTxBytes)
            transactions.push_back(buildFiscoTx(env, f.hashImpl));

        auto const& spec = sample.jovian ? opeth::OP_JOVIAN_SPEC : opeth::OP_ISTHMUS_SPEC;

        // The pre-state is seeded INTO the execution view's own mutable layer (not the
        // backend): computeMptStateRoot is strictly incremental — its delta scan covers the
        // view's TOP MUTABLE LAYER ONLY (MPTBuilder.h buildAndCollect), so a backend-merged
        // seed would be invisible to the trie build.
        std::vector<opeth::OpEthBlockTx> txs;
        txs.reserve(rawTxBytes.size());
        for (std::size_t i = 0; i < rawTxBytes.size(); ++i)
        {
            bool const isDeposit = rawTxBytes[i][0] == opeth::OP_DEPOSIT_TX_TYPE;
            txs.push_back(
                opeth::OpEthBlockTx{.tx = isDeposit ? nullptr : transactions[i],
                    .envelope = rawTxBytes[i]});
        }
        auto view = f.multiLayerStorage.fork();
        view.newMutable();
        opstack_test::seedPreStateIntoView(view, sample.vector["pre"]);
        auto const outcome = runNewPath(f, view, *header, spec, txs);

        BOOST_REQUIRE_EQUAL(outcome.result.receipts.size(), rawTxBytes.size());
        BOOST_REQUIRE_EQUAL(outcome.result.txTypes.size(), rawTxBytes.size());
        for (std::size_t i = 0; i < rawTxBytes.size(); ++i)
            BOOST_CHECK_EQUAL(
                outcome.result.txTypes[i], opeth::opEthClassifyTxType(rawTxBytes[i][0]));

        auto const& expected = sample.vector["_op_expected"];
        checkGoldenHeader(expected["header"], outcome.seal, outcome.stateRoot, outcome.txRoot,
            static_cast<uint64_t>(outcome.result.gasUsed), *header);
        checkGoldenReceipts(expected["receipts"], outcome.result.receipts, rawTxBytes);
        checkGoldenPostState(view, sample.vector["postState"]);
    }
}

}  // namespace

// Same contract as the engine parity suite (OpEngineServiceExecParityTest): a missing corpus on
// a local run warns instead of passing silently; CI (FISCO_REQUIRE_T8N_CORPUS) fails hard.
#define SKIP_IF_NO_T8N_CORPUS()                                                              \
    do                                                                                       \
    {                                                                                        \
        if (!w6test::t8nCorpusAvailable())                                                   \
        {                                                                                    \
            if (w6test::t8nCorpusRequired())                                                 \
            {                                                                                \
                BOOST_FAIL("C1 t8n corpus required in CI but missing at " OP_T8N_VECTORS_DIR \
                           " / " OP_T8N_GOLDEN_ENGINE_DIR);                                  \
            }                                                                                \
            BOOST_WARN_MESSAGE(false, "skipping C1: t8n corpus missing (local run)");        \
            return;                                                                          \
        }                                                                                    \
    } while (0)

BOOST_AUTO_TEST_SUITE(OpEthDualRunSuite)

BOOST_AUTO_TEST_CASE(IsthmusDepositOnly)
{
    SKIP_IF_NO_T8N_CORPUS();
    goldenRunVector("isthmus_deposit_only");
}

BOOST_AUTO_TEST_CASE(JovianDepositOnly)
{
    SKIP_IF_NO_T8N_CORPUS();
    goldenRunVector("jovian_deposit_only");
}

/// The only vector with normal (non-deposit) transactions — exercises the OpPolicy normal-tx
/// path (validateTransaction/runTransaction), not just opRunDeposit.
BOOST_AUTO_TEST_CASE(JovianTransferMulti)
{
    SKIP_IF_NO_T8N_CORPUS();
    goldenRunVector("jovian_transfer_multi");
}

BOOST_AUTO_TEST_SUITE_END()
