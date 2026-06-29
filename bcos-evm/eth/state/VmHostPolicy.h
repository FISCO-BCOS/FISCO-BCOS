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
 * @brief EthHost policy hooks for FISCO vs eth vector execution (spec §5.3, §7.1)
 * @file VmHostPolicy.h
 */

#pragma once

#include <evmc/evmc.h>

namespace bcos::evm::state
{
struct Account;

/// Injectable policy for EthHost extension points (Hook#3–6, #8 via FiscoVmHostPolicy).
/// Chain precompile dispatch is via `ChainCallTargetPort` on FrameContext (ADR-024).
struct VmHostPolicy
{
    virtual ~VmHostPolicy() = default;

    virtual bool allowSelfdestruct(const Account& acc) { return true; }
    virtual bool allowDelegateCallToPrecompile() { return true; }
    virtual bool skipHostValueTransfer() { return false; }

    virtual void prepareMessage(evmc_revision rev, evmc_message& msg)
    {
        (void)rev;
        (void)msg;
    }

    virtual void setCallerAddress(const evmc_address& caller) { (void)caller; }

    virtual void bumpContractCreateNonce(const evmc_address& contractAddress)
    {
        (void)contractAddress;
    }
};
}  // namespace bcos::evm::state
