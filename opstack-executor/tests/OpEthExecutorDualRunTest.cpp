// FISCO BCOS
// SPDX-License-Identifier: Apache-2.0

// OpEthExecutorDualRunTest — golden suite for the t8n corpus with execution driven
// through the production scheduler shape (migration steps 3.1.1/3.2):
//   preBlockOpEthSteps (block-start system calls, deposit-first + Jovian shape,
//   RecentBlockHashes wiring) → SchedulerSerialImpl(serial=true) over OpEthExecutor
//   with an OpEthBlockContext → finalizeOpEthBlockResult (no-reward finalizeState →
//   MessagePasser snapshot → seal → full stateRoot rebuild → txRoot).
// The outcome is asserted against the vector's `_op_expected` golden block via
// support/GoldenExpect.h. Step 3.3 retired both the legacy dual-run baseline leg and this
// file's inline block pre-steps (now the shared OpEthBlockSteps.h stages, driven through
// support/DualRunHarness.h's runExecutorPath); the test-only monolithic executeOpEthBlock
// driver (the old OpEthDualRunTest leg, same battery on the same vectors) was later deleted
// with the production cutover it duplicated.

#include "support/DualRunHarness.h"
#include "support/GoldenExpect.h"
#include "support/GoldenSample.h"  // w6test loaders (pulls support/SeedPreState.h)

#include <boost/test/unit_test.hpp>

#include <bcos-utilities/DataConvertUtility.h>
#include <cstdint>
#include <vector>

using namespace opstack_test;

namespace
{

void goldenRunExecutorVector(std::string const& id)
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

        // Same seeding rule as C1: the pre-state goes INTO the execution view's own mutable
        // layer (computeMptStateRoot is strictly incremental over the top mutable layer).
        auto view = f.multiLayerStorage.fork();
        view.newMutable();
        opstack_test::seedPreStateIntoView(view, sample.vector["pre"]);
        auto outcome = runExecutorPath(f, view, *header, spec, transactions, rawTxBytes);

        BOOST_REQUIRE_EQUAL(outcome.receipts.size(), rawTxBytes.size());

        auto const& expected = sample.vector["_op_expected"];
        checkGoldenHeader(expected["header"], outcome.seal, outcome.stateRoot, outcome.txRoot,
            outcome.gasUsed, *header);
        checkGoldenReceipts(expected["receipts"], outcome.receipts, rawTxBytes);
        checkGoldenPostState(view, sample.vector["postState"]);
    }
}

}  // namespace

// Same contract as the C1 suite: a missing corpus on a local run warns instead of passing
// silently; CI (FISCO_REQUIRE_T8N_CORPUS) fails hard.
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

BOOST_AUTO_TEST_SUITE(OpEthExecutorDualRunSuite)

BOOST_AUTO_TEST_CASE(IsthmusDepositOnly)
{
    SKIP_IF_NO_T8N_CORPUS();
    goldenRunExecutorVector("isthmus_deposit_only");
}

BOOST_AUTO_TEST_CASE(JovianDepositOnly)
{
    SKIP_IF_NO_T8N_CORPUS();
    goldenRunExecutorVector("jovian_deposit_only");
}

/// The only vector with normal (non-deposit) transactions — exercises OpEthExecutor's
/// validateTransaction/runTransaction path under OpPolicy, not just opRunDeposit.
BOOST_AUTO_TEST_CASE(JovianTransferMulti)
{
    SKIP_IF_NO_T8N_CORPUS();
    goldenRunExecutorVector("jovian_transfer_multi");
}

BOOST_AUTO_TEST_SUITE_END()
