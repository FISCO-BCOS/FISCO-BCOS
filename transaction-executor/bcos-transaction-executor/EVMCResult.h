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
#include <evmc/evmc.h>
#include <gsl/pointers>
#include <tuple>

namespace bcos::executor_v1
{
class EVMCResult : public evmc_result
{
public:
    // The status is always explicit: the bit-65 mapper policy is a caller decision,
    // so a constructor that silently applied the default one would hide the flag.
    EVMCResult(evmc_result from, protocol::TransactionStatus statusCode);
    EVMCResult(const EVMCResult&) = delete;
    EVMCResult(EVMCResult&& from) noexcept;
    EVMCResult& operator=(const EVMCResult&) = delete;
    EVMCResult& operator=(EVMCResult&& from) noexcept;
    ~EVMCResult() noexcept;

    protocol::TransactionStatus status;
};

bytes writeErrInfoToOutput(const crypto::Hash& hashImpl, std::string const& errInfo);

std::tuple<gsl::owner<uint8_t*>, size_t, decltype(evmc_result::release)> fillErrorOutputInPlace(
    const crypto::Hash& hashImpl, evmc_status_code status, const std::string& errorInfo = "");

// Mapper with the optional ordinary-failure arm (review F3, gate bit 65). Off (default):
// the historical behaviour — the three ordinary EVM failure statuses fall into default:
// throw, and execute()'s catch arm relabels the receipt (Unknown/OutOfGas per the
// error-handling flag). On: they map to their own statuses like the twin message table.
protocol::TransactionStatus evmcStatusToTransactionStatus(
    evmc_status_code status, bool mapOrdinaryFailures = false);
std::tuple<bcos::protocol::TransactionStatus, bcos::bytes> evmcStatusToErrorMessage(
    const bcos::crypto::Hash& hashImpl, evmc_status_code status);

EVMCResult makeErrorEVMCResult(crypto::Hash const& hashImpl, protocol::TransactionStatus status,
    evmc_status_code evmStatus, int64_t gas, const std::string& errorInfo,
    bool clampGasLeft = false);

}  // namespace bcos::executor_v1

std::ostream& operator<<(std::ostream& output, const evmc_message& message);
std::ostream& operator<<(std::ostream& output, const evmc_result& result);
std::ostream& operator<<(std::ostream& output, const evmc_address& address);