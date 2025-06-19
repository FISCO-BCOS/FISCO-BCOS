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
 * @brief Interface to verify the validity of the transaction
 * @file TxValidatorInterface.h
 * @author: yujiechen
 * @date 2021-05-08
 */
#pragma once
#include "bcos-txpool/txpool/interfaces/NonceCheckerInterface.h"
#include "bcos-txpool/txpool/validator/LedgerNonceChecker.h"
#include "bcos-txpool/txpool/validator/Web3NonceChecker.h"
#include <bcos-framework/protocol/Transaction.h>
#include <bcos-protocol/TransactionStatus.h>

#define TX_VALIDATOR_CHECKER_LOG(LEVEL) BCOS_LOG(LEVEL) << LOG_BADGE("TXValidator") << LOG_BADGE("CHECKER")

namespace bcos::txpool
{
class TxValidatorInterface
{
public:
    using Ptr = std::shared_ptr<TxValidatorInterface>;
    TxValidatorInterface() = default;
    virtual ~TxValidatorInterface() = default;

    virtual bcos::protocol::TransactionStatus verify(const bcos::protocol::Transaction& _tx) = 0;
    virtual bcos::protocol::TransactionStatus checkTransaction(
        const bcos::protocol::Transaction& _tx, bool onlyCheckLedgerNonce = false) = 0;
    virtual bcos::protocol::TransactionStatus checkLedgerNonceAndBlockLimit(
        const bcos::protocol::Transaction& _tx) = 0;
    virtual bcos::protocol::TransactionStatus checkTxpoolNonce(
        const bcos::protocol::Transaction& _tx) = 0;
    virtual bcos::protocol::TransactionStatus checkWeb3Nonce(
        const bcos::protocol::Transaction& _tx, bool onlyCheckLedgerNonce = false) = 0;
    virtual LedgerNonceChecker::Ptr ledgerNonceChecker() = 0;
    virtual Web3NonceChecker::Ptr web3NonceChecker() = 0;
    virtual void setLedgerNonceChecker(LedgerNonceChecker::Ptr _ledgerNonceChecker) = 0;
};
}  // namespace bcos::txpool