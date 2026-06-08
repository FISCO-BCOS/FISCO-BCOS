/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief Regression test for FIB-108: verify clearReceipts prevents stale data
 * @file FIB108_ReceiptCountTest.cpp
 */

#include "bcos-tars-protocol/protocol/BlockImpl.h"
#include "bcos-tars-protocol/protocol/TransactionReceiptImpl.h"
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(FIB108_ReceiptCountTest)

/// Helper: create a receipt with a given gasUsed value for identification.
static bcos::protocol::TransactionReceipt::Ptr makeReceipt(int64_t gasUsed)
{
    auto receipt = std::make_shared<bcostars::protocol::TransactionReceiptImpl>();
    auto& inner = receipt->inner();
    inner.data.gasUsed = std::to_string(gasUsed);
    return receipt;
}

/**
 * Scenario: a block has 5 stale receipts from a prior partial execution.
 * A re-execution produces only 3 new receipts.
 * Without clearReceipts(), the 2 extra stale receipts would remain, leading
 * to receiptsSize() == 5 instead of 3.
 * With clearReceipts() before appending, only the 3 new receipts survive.
 */
BOOST_AUTO_TEST_CASE(ClearReceiptsRemovesStaleEntries)
{
    auto block = std::make_shared<bcostars::protocol::BlockImpl>();

    // Simulate prior execution that left 5 receipts
    for (int i = 0; i < 5; ++i)
    {
        block->appendReceipt(makeReceipt(100 + i));
    }
    BOOST_CHECK_EQUAL(block->receiptsSize(), 5u);

    // Now simulate re-execution: clear then append 3 new receipts
    block->clearReceipts();
    BOOST_CHECK_EQUAL(block->receiptsSize(), 0u);

    for (int i = 0; i < 3; ++i)
    {
        block->appendReceipt(makeReceipt(200 + i));
    }
    BOOST_CHECK_EQUAL(block->receiptsSize(), 3u);

    // Verify the receipts are the new ones (gasUsed 200, 201, 202)
    size_t idx = 0;
    for (auto receipt : block->receipts())
    {
        auto expectedGas = std::to_string(200 + static_cast<int>(idx));
        BOOST_CHECK_EQUAL(receipt->gasUsed().str(), expectedGas);
        ++idx;
    }
    BOOST_CHECK_EQUAL(idx, 3u);
}

/**
 * Edge case: clearReceipts on an already-empty block is a no-op.
 */
BOOST_AUTO_TEST_CASE(ClearReceiptsOnEmptyBlock)
{
    auto block = std::make_shared<bcostars::protocol::BlockImpl>();
    BOOST_CHECK_EQUAL(block->receiptsSize(), 0u);

    block->clearReceipts();
    BOOST_CHECK_EQUAL(block->receiptsSize(), 0u);

    // Can still append after clearing empty
    block->appendReceipt(makeReceipt(42));
    BOOST_CHECK_EQUAL(block->receiptsSize(), 1u);
}

BOOST_AUTO_TEST_SUITE_END()
