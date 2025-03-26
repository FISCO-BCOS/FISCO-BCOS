/*
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
 * @file Common.h
 * @author: ancelmo
 * @date: 2021-10-08
 */

#pragma once
#include "bcos-crypto/interfaces/crypto/Hash.h"
#include "bcos-protocol/TransactionStatus.h"
#include <evmc/instructions.h>

namespace bcos::executor_v1
{
class EVMCResult : public evmc_result
{
public:
    explicit EVMCResult(evmc_result&& from);
    EVMCResult(evmc_result&& from, protocol::TransactionStatus _status);
    EVMCResult(const EVMCResult&) = delete;
    EVMCResult(EVMCResult&& from) noexcept;
    EVMCResult& operator=(const EVMCResult&) = delete;
    EVMCResult& operator=(EVMCResult&& from) noexcept;
    ~EVMCResult() noexcept;

    protocol::TransactionStatus status;
};

bytes writeErrInfoToOutput(const crypto::Hash& hashImpl, std::string const& errInfo);

protocol::TransactionStatus evmcStatusToTransactionStatus(evmc_status_code status);
std::tuple<bcos::protocol::TransactionStatus, bcos::bytes> evmcStatusToErrorMessage(
    const bcos::crypto::Hash& hashImpl, evmc_status_code status);

EVMCResult makeErrorEVMCResult(crypto::Hash const& hashImpl, protocol::TransactionStatus status,
    evmc_status_code evmStatus, int64_t gas, const std::string& errorInfo);

}  // namespace bcos::executor_v1

std::ostream& operator<<(std::ostream& output, const evmc_message& message);
std::ostream& operator<<(std::ostream& output, const evmc_result& result);