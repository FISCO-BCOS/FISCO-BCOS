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
 * @file FIB179_180_EVMCResultTest.cpp
 * @date 2026
 */

#include "../bcos-transaction-executor/EVMCResult.h"
#include "bcos-crypto/hash/Keccak256.h"
#include "bcos-protocol/TransactionStatus.h"
#include <evmc/evmc.h>
#include <boost/test/unit_test.hpp>
#include <utility>
using namespace bcos;
using namespace bcos::executor_v1;
using namespace bcos::protocol;

BOOST_AUTO_TEST_SUITE(FIB179_180_EVMCResultTest)

BOOST_AUTO_TEST_CASE(moveAssign_updatesCachedStatus)
{
    evmc_result dstResult = {};
    dstResult.status_code = EVMC_SUCCESS;
    dstResult.release = nullptr;
    evmc_result srcResult = {};
    srcResult.status_code = EVMC_REVERT;
    srcResult.release = nullptr;

    EVMCResult dst(dstResult);
    EVMCResult src(srcResult);
    dst = std::move(src);
    BOOST_CHECK_EQUAL(dst.status_code, EVMC_REVERT);
    BOOST_CHECK_EQUAL(dst.status, TransactionStatus::RevertInstruction);
}

BOOST_AUTO_TEST_SUITE_END()
