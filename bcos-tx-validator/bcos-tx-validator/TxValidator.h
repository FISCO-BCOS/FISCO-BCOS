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
#include "bcos-framework/ledger/LedgerConfig.h"
#include "bcos-framework/ledger/LedgerInterface.h"
#include "bcos-framework/protocol/Transaction.h"
#include "bcos-protocol/TransactionStatus.h"
#include "bcos-task/Task.h"
#include "bcos-tx-validator/CheckSet.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>

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
    u256 nonce;
    bytes code;  ///< empty = EOA; non-empty and not 0xef0100-prefixed = contract, EIP-3607 rejects
};

/// std::nullopt means the account does not exist (balance 0, nonce 0, no code).
///
/// A FAILURE to read (neither scheduler nor ledger reachable) must NOT be reported as nullopt or
/// as a TransactionStatus -- it throws. Today TxValidator.cpp:216-221 swallows a scheduler
/// failure, leaves balanceValue at 0 and returns InsufficientFunds, so a storage outage is
/// reported to the user as "your balance is too low" and the real cause is unfindable.
using AccountStateReader = std::function<task::Task<std::optional<AccountState>>(std::string_view)>;

/// Proposal verification needs only the account nonce. It is a consensus hot path -- every
/// transaction of every block -- so it does not use AccountStateReader, which would additionally
/// fetch balance and contract code that its check set never looks at.
using AccountNonceReader = std::function<task::Task<std::optional<u256>>(std::string_view)>;

/// The one seam between this module and a transaction pool. Neither pool's public interface
/// changes: the old pool binds this to the two BCOS nonce checkers it already owns privately,
/// and the mempool leaves it empty (engine-driven mode initialises no txpool, so it sees only
/// Web3 transactions).
///
/// Web3 nonce rules are deliberately NOT here -- they need only the account nonce, which comes
/// from AccountStateReader.
struct PoolNonceQuery
{
    /// BCOS txpool nonce + ledger nonce + blockLimit. Second argument is onlyCheckLedgerNonce.
    std::function<protocol::TransactionStatus(protocol::Transaction const&, bool)> checkBcosNonce;
};

/// Whether a transaction targets a system contract, injected rather than computed here: the
/// answer needs precompiled::c_systemTxsAddress, and linking bcos-executor for one address list
/// would drag evmone -- and wedprcrypto's bundled runtime, which breaks libc++ exception
/// catching binary-wide -- into every module that admits a transaction.
using SystemTxPredicate = std::function<bool(protocol::Transaction const&)>;

/// Stateless. Everything it needs is either the transaction, an injected reader, or one call
/// through PoolNonceQuery -- so it can be unit-tested with fakes, without building a pool or a
/// block sequence.
class TxValidator
{
public:
    using LedgerConfigProvider = std::function<task::Task<ledger::LedgerConfig::Ptr>()>;

    TxValidator(crypto::CryptoSuite::Ptr cryptoSuite,
        std::shared_ptr<ledger::LedgerInterface> ledger, LedgerConfigProvider ledgerConfigProvider,
        AccountStateReader accountStateReader, AccountNonceReader accountNonceReader,
        SystemTxPredicate isSystemTx, std::string groupId, std::string chainId);

    /// Run the check set for (kind of @p tx, @p context, @p policy), in c_checkOrder order.
    ///
    /// @p tx is non-const: normalization rewrites the mirror, signature verification writes the
    /// recovered sender, and a system transaction gets setSystemTx(true).
    ///
    /// Returns the first failing check's status, or None. Throws (does not return a status) when
    /// the data needed to decide is unavailable -- see AccountStateReader.
    task::Task<protocol::TransactionStatus> admit(protocol::Transaction& tx,
        AdmissionContext context, SignaturePolicy policy, PoolNonceQuery const& pool);

private:
    crypto::CryptoSuite::Ptr m_cryptoSuite;
    std::shared_ptr<ledger::LedgerInterface> m_ledger;
    LedgerConfigProvider m_ledgerConfigProvider;
    AccountStateReader m_accountStateReader;
    AccountNonceReader m_accountNonceReader;
    SystemTxPredicate m_isSystemTx;
    std::string m_groupId;
    std::string m_chainId;
};

}  // namespace bcos::txvalidator
