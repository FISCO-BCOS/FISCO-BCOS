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
 * @file AdmissionAdapters.cpp
 * @date 2026/8/25
 */

#include "bcos-txpool/txpool/validator/AdmissionAdapters.h"
#include "bcos-framework/ledger/LedgerTypeDef.h"
#include "bcos-ledger/LedgerMethods.h"
#include "bcos-txpool/txpool/utilities/Common.h"
#include <boost/lexical_cast.hpp>

using namespace bcos;
using namespace bcos::protocol;

namespace bcos::txpool
{
namespace
{
}  // namespace

txvalidator::AccountStateReader makeAccountStateReader(SchedulerHolder::Ptr schedulerHolder,
    std::shared_ptr<bcos::ledger::LedgerInterface> ledger, Web3NonceChecker::Ptr web3NonceChecker)
{
    return [schedulerHolder = std::move(schedulerHolder), ledger = std::move(ledger),
               web3NonceChecker = std::move(web3NonceChecker)](
               std::string_view sender) -> task::Task<std::optional<txvalidator::AccountState>> {
        txvalidator::AccountState state;

        // Committed plane, through the FIB-59 cache. A miss means the account is not on chain,
        // and that must be reported as "absent" rather than flattened into nonce 0: the nonce
        // window declines to judge an unknown account (as the existing Web3NonceChecker does),
        // and collapsing the two would start rejecting first-time senders whose nonce sits
        // beyond the queue window measured from zero.
        // A miss leaves nonce unset rather than 0 -- see AccountState::nonce. Balance is read
        // regardless: an account absent from the committed plane may still hold a pending
        // balance.
        state.nonce = co_await web3NonceChecker->committedNonce(sender);

        auto const senderHex = toHex(sender);
        auto scheduler = schedulerHolder->scheduler.lock();
        if (!scheduler)
        {
            // No scheduler: fall back to the COMMITTED balance from the ledger. This is the
            // fallback the old validator's comment described ("fallback to ledger") but never
            // performed -- it logged and left the balance at 0, so every transaction came back
            // as InsufficientFunds whenever the scheduler was missing.
            //
            // Contract account code is unreadable without the scheduler, so `code` stays empty
            // and the EIP-3607 check consequently passes. That is a deliberate weakening of ONE
            // check in a configuration where the alternative is refusing all traffic; balance
            // and nonce, which is what admission mostly turns on, are still enforced.
            if (auto const storageState = co_await ledger->getStorageState(senderHex, 0))
            {
                if (auto const& balance = storageState.value().balance; !balance.empty())
                {
                    state.balance = u256(balance);
                }
            }
            co_return state;
        }

        auto const blockNumber = co_await ledger::getCurrentBlockNumber(*ledger);
        // Pending plane: the latest executed-but-uncommitted layer.
        if (auto entry = co_await scheduler->getPendingStorageAt(
                senderHex, ledger::ACCOUNT_TABLE_FIELDS::BALANCE, blockNumber))
        {
            if (auto const value = entry->get(); !value.empty())
            {
                state.balance = boost::lexical_cast<u256>(value);
            }
        }
        // Contract code is deliberately NOT read here, so `code` stays empty and the EIP-3607
        // sender-is-an-EOA check passes on this path.
        //
        // The only way to it is SchedulerInterface::getCode, which is callback-based and
        // completes on the calling thread. MemoryStorage::verifyAndSubmitTransaction is
        // synchronous and drives admission through task::syncWait, so awaiting getCode there
        // blocks the very thread that has to run the callback -- a deadlock, not a slowdown.
        // getPendingStorageAt above is safe because it is already a coroutine.
        //
        // Consequence, stated plainly: on the AIR path EIP-3607 is not enforced at admission.
        // It still is at execution, and the engine-driven path (which reads a real state
        // snapshot) enforces it at admission too. Lifting this needs either an async submit
        // chain or a coroutine getCode, both out of scope here.
        co_return state;
    };
}

txvalidator::AccountNonceReader makeAccountNonceReader(Web3NonceChecker::Ptr web3NonceChecker)
{
    return [web3NonceChecker = std::move(web3NonceChecker)](
               std::string_view sender) -> task::Task<std::optional<u256>> {
        co_return co_await web3NonceChecker->committedNonce(sender);
    };
}

txvalidator::PoolNonceQuery makePoolNonceQuery(
    NonceCheckerInterface::Ptr txPoolNonceChecker, TxValidatorInterface::Ptr validator)
{
    return txvalidator::PoolNonceQuery{
        .checkBcosNonce = [txPoolNonceChecker = std::move(txPoolNonceChecker),
                              validator = std::move(validator)](
                              Transaction const& tx, bool onlyCheckLedgerNonce) {
            // Resolved per call, not captured: setLedgerNonceChecker runs after the pool is built.
            auto ledgerNonceChecker = validator ? validator->ledgerNonceChecker() : nullptr;
            // Body of the old TxValidator::checkTransaction, minus the Web3 branch (Web3 nonce
            // rules need only the account nonce and now live in the admission layer).
            if (!onlyCheckLedgerNonce && txPoolNonceChecker)
            {
                if (auto status = txPoolNonceChecker->checkNonce(tx);
                    status != TransactionStatus::None)
                {
                    return status;
                }
            }
            if (!ledgerNonceChecker)
            {
                // Set only after the pool learns the chain's block limit; before that there is
                // nothing to check against.
                return TransactionStatus::None;
            }
            return ledgerNonceChecker->checkNonce(tx);
        }};
}

}  // namespace bcos::txpool
