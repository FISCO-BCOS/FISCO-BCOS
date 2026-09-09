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
}

BOOST_AUTO_TEST_CASE(detailIsAppendedInParentheses)
{
    auto const e = admissionError(TS::BlobTxNotAllowed, "blob");
    BOOST_CHECK_EQUAL(e.code(), Web3DefaultError);
    BOOST_CHECK_EQUAL(e.msg(), "transaction type not supported (blob)");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
