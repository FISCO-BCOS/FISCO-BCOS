/**
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
 * @file AdmissionErrorTest.cpp
 * @author: kyonGuo
 * @date 2026/9/9
 */

#include <bcos-protocol/TransactionStatus.h>
#include <bcos-rpc/jsonrpc/Common.h>
#include <bcos-rpc/web3jsonrpc/utils/AdmissionError.h>
#include <bcos-rpc/web3jsonrpc/utils/Common.h>
#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <limits>

using namespace bcos;
using namespace bcos::rpc;
using TS = protocol::TransactionStatus;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(AdmissionErrorTest)

// The three structural refusals are the only -32602 rows; geth's words where it has them.
BOOST_AUTO_TEST_CASE(structuralRefusalsAreInvalidParams)
{
    auto const chain = admissionError(TS::InvalidChainId);
    BOOST_CHECK_EQUAL(chain.code(), InvalidParams);
    BOOST_CHECK_EQUAL(chain.msg(), "invalid chain id for signer");
    auto const sig = admissionError(TS::InvalidSignature);
    BOOST_CHECK_EQUAL(sig.code(), InvalidParams);
    BOOST_CHECK_EQUAL(sig.msg(), "invalid sender");
    // No geth words for this one: the code changes, the name stays.
    auto const malformed = admissionError(TS::Malformed);
    BOOST_CHECK_EQUAL(malformed.code(), InvalidParams);
    BOOST_CHECK_EQUAL(malformed.msg(), protocol::toString(TS::Malformed));
}

// Unknown is the node failing to decide, not the transaction failing a rule.
BOOST_AUTO_TEST_CASE(undecidedAdmissionIsTheNodesFault)
{
    auto const e = admissionError(TS::Unknown);
    BOOST_CHECK_EQUAL(e.code(), InternalError);
    BOOST_CHECK_EQUAL(e.msg(), "admission could not be decided");
}

// A well-formed transaction a rule turned away: -32000, in geth's words.
BOOST_AUTO_TEST_CASE(refusedByRuleIsServerErrorInGethWords)
{
    struct Row
    {
        TS status;
        std::string_view message;
    };
    for (auto const& [status, message] : {
             Row{TS::InsufficientFunds, "insufficient funds for gas * price + value"},
             Row{TS::AlreadyInTxPool, "already known"},
             Row{TS::TxPoolIsFull, "txpool is full"},
             Row{TS::OutOfGasLimit, "intrinsic gas too low"},
             Row{TS::TipGreaterThanFeeCap, "max priority fee per gas higher than max fee per gas"},
             Row{TS::FeeCapLessThanBaseFee, "max fee per gas less than block base fee"},
             Row{TS::TxTypeNotSupported, "transaction type not supported"},
             Row{TS::BlobTxNotAllowed, "transaction type not supported"},
             Row{TS::SenderNoEOA, "sender not an eoa"},
             Row{TS::NonceHasMaxValue, "nonce has max value"},
             Row{TS::MaxInitCodeSizeExceeded, "max initcode size exceeded"},
             Row{TS::CreateSetCodeTx, "EIP-7702 transaction cannot be used to create contract"},
             Row{TS::EmptyAuthorizationList, "EIP-7702 transaction with empty auth list"},
         })
    {
        auto const e = admissionError(status);
        BOOST_CHECK_EQUAL(e.code(), Web3DefaultError);
        BOOST_CHECK_EQUAL(e.msg(), message);
    }
}

// A status geth has no words for keeps its name, still at -32000: the code says "refused", the
// name says why, and nothing pretends to be a geth error it is not. NonceCheckFail and
// MaxGasLimitExceeded are in this group deliberately: each covers several of geth's sentences
// ("nonce too low" / "nonce too high" / a held (sender, nonce); "exceeds block gas limit" /
// "transaction gas limit too high"), and claiming one would be wrong the rest of the time.
BOOST_AUTO_TEST_CASE(statusWithoutGethWordsKeepsItsName)
{
    auto const nonce = admissionError(TS::NonceCheckFail);
    BOOST_CHECK_EQUAL(nonce.code(), Web3DefaultError);
    BOOST_CHECK_EQUAL(nonce.msg(), "NonceCheckFail");
    auto const gasLimit = admissionError(TS::MaxGasLimitExceeded);
    BOOST_CHECK_EQUAL(gasLimit.code(), Web3DefaultError);
    BOOST_CHECK_EQUAL(gasLimit.msg(), "MaxGasLimitExceeded");
    auto const limit = admissionError(TS::BlockLimitCheckFail);
    BOOST_CHECK_EQUAL(limit.code(), Web3DefaultError);
    BOOST_CHECK_EQUAL(limit.msg(), "BlockLimitCheckFail");
    // Not a verdict on the transaction: the pool admitted it and then ended a sync wait without
    // executing it (MemoryStorage::removeInvalidTxs). geth has no words for it, it keeps its name.
    auto const timeout = admissionError(TS::TransactionPoolTimeout);
    BOOST_CHECK_EQUAL(timeout.code(), Web3DefaultError);
    BOOST_CHECK_EQUAL(timeout.msg(), "TransactionPoolTimeout");
    // Below the table's first row (Unknown = 1), where the binary search stops at begin() without
    // matching. EthEndpoint casts an Error's code straight to TransactionStatus, so an arbitrary
    // int -- None among them -- reaches here.
    auto const none = admissionError(TS::None);
    BOOST_CHECK_EQUAL(none.code(), Web3DefaultError);
    BOOST_CHECK_EQUAL(none.msg(), "None");
}

BOOST_AUTO_TEST_CASE(detailIsAppendedInParentheses)
{
    auto const e = admissionError(TS::BlobTxNotAllowed, "blob");
    BOOST_CHECK_EQUAL(e.code(), Web3DefaultError);
    BOOST_CHECK_EQUAL(e.msg(), "transaction type not supported (blob)");
}

// Which codes the table is allowed to answer at all. An in-process pool only ever throws a
// TransactionStatus, but the same interface carries a MAX/TARS client's own faults through the
// same int field, and the table's fallback would name one of those "Unknown" -- a refusal's code
// for a node fault. A code this node has no name for is not a verdict.
BOOST_AUTO_TEST_CASE(onlyANamedStatusIsAVerdict)
{
    BOOST_TEST(isAdmissionVerdict(static_cast<int32_t>(TS::TxPoolIsFull)));
    // Named, and refused by the fallback rather than by a row -- still a verdict.
    BOOST_TEST(isAdmissionVerdict(static_cast<int32_t>(TS::NonceCheckFail)));
    // The one status whose own name is the name every undeclared value gets.
    BOOST_TEST(isAdmissionVerdict(static_cast<int32_t>(TS::Unknown)));
    // Not a refusal: nothing refused it.
    BOOST_TEST(!isAdmissionVerdict(static_cast<int32_t>(TS::None)));
    // TxPoolServiceClient's await_resume when the tars callback left no value.
    BOOST_TEST(!isAdmissionVerdict(-1));
    // toBcosError(tars::Int32): a TARS transport code.
    BOOST_TEST(!isAdmissionVerdict(-7));
    BOOST_TEST(!isAdmissionVerdict(99999));
    BOOST_TEST(!isAdmissionVerdict(std::numeric_limits<int32_t>::min()));
    BOOST_TEST(!isAdmissionVerdict(std::numeric_limits<int32_t>::max()));
    // Error's code is an int64_t: a value outside TransactionStatus's own type must not become a
    // status by truncation. 0x1'0000'2712 truncates to TxPoolIsFull.
    BOOST_TEST(!isAdmissionVerdict(int64_t{0x100002712}));
    BOOST_TEST(!isAdmissionVerdict(std::numeric_limits<int64_t>::min()));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
