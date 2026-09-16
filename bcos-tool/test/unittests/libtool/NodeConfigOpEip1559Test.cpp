/*
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 */

// [op_eip1559]: the chain's EIP-1559 triple (op-deployer's config.optimism, the same numbers
// rollup.json carries as chain_op_config) as a chain-level, genesis-frozen key. The engine
// hardcoded the OP mainnet preset before this section existed, so a chain with its own
// denominator (the corpus devnet and the C2 e2e both declare 8) priced every pre-Canyon block
// differently from its own op-geth — and engine_newPayload rejected those valid blocks.

#include "ExceptionCheck.h"
#include <bcos-crypto/signature/key/KeyFactoryImpl.h>
#include <bcos-framework/engine/OpEip1559Params.h>
#include <bcos-tool/NodeConfig.h>
#include <boost/test/unit_test.hpp>
#include <optional>
#include <string>

using namespace bcos;
using namespace bcos::tool;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(NodeConfigOpEip1559Test)

BOOST_AUTO_TEST_CASE(effectiveValueFallsBackToTheLegacyPreset)
{
    BOOST_CHECK_EQUAL(bcos::engine::effectiveOpEip1559(std::nullopt).elasticity, 6U);
    BOOST_CHECK_EQUAL(bcos::engine::effectiveOpEip1559(std::nullopt).denominator, 50U);
    BOOST_CHECK_EQUAL(bcos::engine::effectiveOpEip1559(std::nullopt).denominatorCanyon, 250U);

    bcos::engine::OpEip1559Params const declared{
        .elasticity = 2, .denominator = 8, .denominatorCanyon = 250};
    BOOST_CHECK_EQUAL(bcos::engine::effectiveOpEip1559(declared).elasticity, 2U);
    BOOST_CHECK_EQUAL(bcos::engine::effectiveOpEip1559(declared).denominator, 8U);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
