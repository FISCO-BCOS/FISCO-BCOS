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
 * @brief EVMC/WASM compatibility extensions
 * @file EVMCWasm.h
 */

#pragma once

#include <evmc/evmc.h>

// FISCO-BCOS custom gas metrics (originally in evmc fork, now defined locally).
struct evmc_gas_metrics
{
    int16_t createGas;
    int16_t sstoreSetGas;
    int16_t sstoreResetGas;
    int16_t sloadGas;
    int16_t valueTransferGas;
    int16_t callStipend;
    int16_t callNewAccount;
};

// WASM host interface (originally in evmc fork, defined locally for evmone 0.21.0).
struct wasm_host_interface
{
    bool (*accountExists)(evmc_host_context*, const uint8_t*, int32_t) noexcept;
    int32_t (*get)(
        evmc_host_context*, const uint8_t*, int32_t, const uint8_t*, int32_t, uint8_t*, int32_t);
    evmc_storage_status (*set)(evmc_host_context*, const uint8_t*, int32_t, const uint8_t*, int32_t,
        const uint8_t*, int32_t);
    size_t (*getCodeSize)(evmc_host_context*, const uint8_t*, int32_t);
    evmc_bytes32 (*getCodeHash)(evmc_host_context*, const uint8_t*, int32_t);
    size_t (*copyCode)(evmc_host_context*, const uint8_t*, int32_t, size_t, uint8_t*, size_t);
    void (*log)(evmc_host_context*, const uint8_t*, int32_t, uint8_t const*, size_t,
        const evmc_bytes32[], size_t) noexcept;
    evmc_result (*call)(evmc_host_context*, const evmc_message*) noexcept;
    evmc_tx_context (*getTxContext)(evmc_host_context*) noexcept;
    evmc_bytes32 (*getBlockHash)(evmc_host_context*, int64_t);
    void (*emitEvent)(evmc_host_context*, const uint8_t*, int32_t, const uint8_t*, int32_t,
        const uint8_t* const*, const int32_t*, int32_t) noexcept;
    evmc_result (*delegateCall)(evmc_host_context*, const evmc_message*) noexcept;
    evmc_bytes32 (*getBalance)(evmc_host_context*, const uint8_t*, int32_t) noexcept;
    void (*selfdestruct)(
        evmc_host_context*, const uint8_t*, int32_t, const uint8_t*, int32_t) noexcept;
};
