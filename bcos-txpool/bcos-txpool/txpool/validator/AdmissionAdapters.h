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
 * @file AdmissionAdapters.h
 * @brief AIR-mode readers that back bcos-tx-validator's injected dependencies.
 * @date 2026/8/25
 */

#pragma once

#include "bcos-framework/dispatcher/SchedulerInterface.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-tx-validator/TxValidator.h"
#include "bcos-txpool/txpool/interfaces/NonceCheckerInterface.h"
#include "bcos-txpool/txpool/validator/LedgerNonceChecker.h"
#include "bcos-txpool/txpool/validator/Web3NonceChecker.h"
#include <memory>

namespace bcos::txpool
{

/// Mutable holder for the scheduler, which TxPoolFactory only learns about after the pool has
/// been built (createTxPool runs first, setScheduler later). The readers below capture this
/// rather than a scheduler, so they are constructed once and pick the scheduler up when it
/// arrives.
struct SchedulerHolder
{
    using Ptr = std::shared_ptr<SchedulerHolder>;
    std::weak_ptr<bcos::scheduler::SchedulerInterface> scheduler;
};

/// Balance, nonce and code for the pool-admission path.
///
/// The three come from TWO different planes, deliberately: balance from the scheduler's PENDING
/// state (the latest executed-but-uncommitted layer) and nonce from the COMMITTED ledger, via
/// Web3NonceChecker's cache. That is the mix AIR already has today, and unifying it would cost
/// pending-balance visibility -- an account credited by an executed-but-uncommitted block can
/// send a transaction now and could not afterwards. A torn read only mis-admits one transaction;
/// execution is the authority and every node executes the same state, so consensus is unaffected.
///
/// The nonce MUST go through Web3NonceChecker::committedNonce so the m_ledgerStateNonces cache is
/// preserved -- reading through to storage per transaction would undo FIB-59.
///
/// A read failure propagates as an exception. It is not reported as "account missing", which
/// would surface a storage outage to the user as InsufficientFunds.
txvalidator::AccountStateReader makeAccountStateReader(SchedulerHolder::Ptr schedulerHolder,
    std::shared_ptr<bcos::ledger::LedgerInterface> ledger, Web3NonceChecker::Ptr web3NonceChecker);

/// Nonce only, for the consensus proposal path.
txvalidator::AccountNonceReader makeAccountNonceReader(Web3NonceChecker::Ptr web3NonceChecker);

/// The BCOS nonce/blockLimit seam. Binds the two checkers the pool already owns privately, so
/// neither TxPoolInterface nor the MemPool concept has to grow a method.
///
/// Takes the validator rather than a LedgerNonceChecker: that checker is created later, once the
/// chain's block limit is known (setLedgerNonceChecker), so capturing it here would capture null
/// and silently disable blockLimit checking for the life of the process. Resolved per call.
/// Resolves the LedgerNonceChecker at call time. It does not exist until TxPool::init learns
/// the chain's block limit, so it cannot be captured by value when the query is built.
using LedgerNonceCheckerProvider = std::function<LedgerNonceChecker::Ptr()>;

txvalidator::PoolNonceQuery makePoolNonceQuery(
    NonceCheckerInterface::Ptr txPoolNonceChecker, LedgerNonceCheckerProvider ledgerNonceChecker);

}  // namespace bcos::txpool
