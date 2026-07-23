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
 * @brief FISCO-BCOS custom gas metrics for EVMC
 * @file EVMCGasMetrics.h
 */

#pragma once

#include <evmc/evmc.h>

// FISCO-BCOS custom gas metrics. evmc.h only forward-declares this struct and holds a pointer
// to it in evmc_host_context; the definition lives here. It previously shared EVMCWasm.h with
// wasm_host_interface, but only that interface was WASM-specific -- the EVM path needs these
// metrics via ethMetrics in Common.h, so the struct outlived the WASM removal.
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
