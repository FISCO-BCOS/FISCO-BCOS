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
 * @file TxValidator.h
 * @brief One admission entry point for both transaction pools.
 * @date 2026/8/25
 */

#pragma once

#include "bcos-crypto/interfaces/crypto/CryptoSuite.h"
#include "bcos-framework/dispatcher/SchedulerInterface.h"
#include "bcos-framework/ledger/LedgerConfigState.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-task/Task.h"
#include "bcos-tx-validator/CheckSet.h"
#include "bcos-tx-validator/LedgerNonceChecker.h"
#include "bcos-tx-validator/NonceCheckerInterface.h"
#include "bcos-tx-validator/Web3NonceChecker.h"
#include "bcos-utilities/Common.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace bcos::txvalidator
{

/// Whether `to` is a well-formed recipient: empty (contract creation) or 20 hex bytes.
/// Issue #5318 -- a malformed `to` accepted at admission reaches a block and stops the chain.
bool isValidToField(std::string_view toField);

/// What the account-state checks need. No codeHash: EIP-3607 is decided on the code BYTES
/// (evmone::is_code_delegated recognises the 0xef0100 delegation prefix), and `code.empty()` is
/// equivalent to `codeHash == EMPTY_CODE_HASH`. AIR has no ready codeHash read anyway --
/// Ledger::getStorageState returns only nonce and balance -- so one fewer field is one fewer read.
struct AccountState
{
    u256 balance;
    /// std::nullopt = the account has no on-chain state yet. Distinct from nonce 0: the nonce
    /// window declines to judge an unknown account (matching the existing Web3NonceChecker),
    /// whereas nonce 0 is a real lower bound. Collapsing the two would reject a first-time
    /// sender whose nonce sits beyond the queue window measured from zero -- while balance may
    /// still be readable for that same account from the pending plane.
    std::optional<u256> nonce;
    bytes code;  ///< empty = EOA; non-empty and not 0xef0100-prefixed = contract, EIP-3607 rejects
};

/// Whether a transaction targets a system contract, injected rather than computed here: the
/// answer needs precompiled::c_systemTxsAddress, and linking bcos-executor for one address list
/// would drag evmone -- and wedprcrypto's bundled runtime, which breaks libc++ exception
/// catching binary-wide -- into every module that admits a transaction.
using SystemTxPredicate = std::function<bool(protocol::Transaction const&)>;

/// The one place a transaction is judged admissible, for every ingress: Web3 JSON-RPC, P2P, and
/// block-proposal verification. As of this commit no ingress calls it yet -- the pool still runs
/// txpool::TxValidator, and the callers move over in the wiring commits that follow. Read the
/// sentence above as what this class is for, not as a claim about who calls it today.
///
/// It OWNS the nonce checkers rather than reaching them through callbacks. Nonce admission is
/// the same question at every ingress, and routing it through a per-caller hook is how the pool
/// and the RPC layer came to disagree about it in the first place.
class TxValidator
{
public:
    /// @p ledgerConfigState is read, never written: whoever commits a block publishes the new
    /// configuration there, and verify() picks it up on the next transaction. Passing a
    /// snapshot holder rather than the configuration itself is what keeps a long-lived
    /// TxValidator from pinning the genesis config forever.
    ///
    /// @p ledgerConfigState and @p web3NonceChecker must be non-null and throw if they are not;
    /// @p txPoolNonceChecker and the two late-bound setters below are nullable by design, each
    /// with a check that says what its absence means.
    TxValidator(crypto::CryptoSuite::Ptr cryptoSuite,
        std::shared_ptr<ledger::LedgerInterface> ledger,
        ledger::LedgerConfigState::Ptr ledgerConfigState,
        NonceCheckerInterface::Ptr txPoolNonceChecker, Web3NonceChecker::Ptr web3NonceChecker,
        SystemTxPredicate isSystemTx, std::string groupId, std::string chainId);

    /// Bound after construction: the ledger nonce checker cannot be built until the pool has read
    /// the chain's block limit. Until it is bound there is nothing to check a BCOS nonce against,
    /// and that check passes.
    void setLedgerNonceChecker(std::shared_ptr<LedgerNonceChecker> ledgerNonceChecker);

    /// Also bound after construction -- the scheduler does not exist yet when the pools are
    /// built. Until it is bound, and in engine-driven mode where there is none, the balance check
    /// falls back to the committed ledger balance instead of the pending plane.
    void setScheduler(std::weak_ptr<scheduler::SchedulerInterface> scheduler);

    /// Run the check set for (kind of @p tx, @p context, @p policy), in c_checkOrder order.
    ///
    /// @p tx is non-const: normalization rewrites the mirror, signature verification writes the
    /// recovered sender, and a system transaction gets setSystemTx(true).
    ///
    /// Returns the first failing check's status, or None. Throws (does not return a status) when
    /// the data needed to decide is unavailable: a storage outage must not be reported to the
    /// user as "your balance is too low", which is what swallowing the error would produce.
    task::Task<protocol::TransactionStatus> verify(
        protocol::Transaction& tx, AdmissionContext context, SignaturePolicy policy);

    virtual ~TxValidator() = default;

protected:
    /// Balance, nonce and code for @p sender. Balance comes from the scheduler's PENDING plane
    /// when a scheduler is bound and from the committed ledger otherwise; nonce always comes
    /// from the COMMITTED plane through Web3NonceChecker's cache. That split is what AIR
    /// already does, and unifying it is a separate change.
    ///
    /// `code` is left EMPTY on every path, so the EIP-3607 sender-is-an-EOA check cannot
    /// currently fire; the implementation states what filling it correctly would require.
    /// virtual so a test can supply code and pin the rule itself, which stays correct and starts
    /// mattering the moment this fills the field.
    ///
    /// @p sender is a view into the transaction verify() was handed, and is read after this
    /// coroutine's awaits: that is sound because the caller keeps the transaction alive for the
    /// whole of verify(), the ordinary contract for a Task-returning member.
    virtual task::Task<std::optional<AccountState>> readAccountState(std::string_view sender);

private:
    crypto::CryptoSuite::Ptr m_cryptoSuite;
    std::shared_ptr<ledger::LedgerInterface> m_ledger;
    ledger::LedgerConfigState::Ptr m_ledgerConfigState;
    NonceCheckerInterface::Ptr m_txPoolNonceChecker;
    Web3NonceChecker::Ptr m_web3NonceChecker;
    SystemTxPredicate m_isSystemTx;
    std::string m_groupId;
    std::string m_chainId;
    /// The two late-bound dependencies, both written once from an init path that runs while
    /// transactions may already be arriving, hence the lock. Readers copy the handle out and
    /// release, so nothing is held across a co_await.
    ///
    /// The scheduler is weak on purpose: a strong edge would have the pool and the scheduler
    /// co-own each other.
    mutable SharedMutex x_lateBound;
    /// Not LedgerNonceChecker::Ptr: that name is inherited from NonceCheckerInterface
    /// and resolves to shared_ptr<NonceCheckerInterface>, which loses the derived type.
    std::shared_ptr<LedgerNonceChecker> m_ledgerNonceChecker;
    std::weak_ptr<scheduler::SchedulerInterface> m_scheduler;
};

}  // namespace bcos::txvalidator
