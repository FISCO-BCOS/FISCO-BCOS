/*
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
 * @file: ScopedNodeAddressTableMode.h
 * RAII guard for the process-global account-table mode in tests/benchmarks
 */

#pragma once

#include "bcos-framework/ledger/AccountTableName.h"

namespace bcos::test
{
/// Sets the process-global account-table mode (ledger/account/AccountTableName.h) on
/// construction and restores the previous value on destruction. Production startup sets the
/// singleton exactly once, single-threaded, before any reader starts; tests that flip it must
/// not leak the flipped value into the cases that run after them in the same binary — a
/// BOOST_REQUIRE failure (or any early return/throw) skips a trailing restore call, but never
/// a destructor. Construct one at the top of the test (or as a fixture member); mid-test mode
/// changes can still call setNodeAddressTableMode directly, the guard restores the value that
/// predates the guard itself.
class ScopedNodeAddressTableMode
{
public:
    explicit ScopedNodeAddressTableMode(ledger::account::AddressTableMode mode) noexcept
      : m_previous(ledger::account::nodeAddressTableMode())
    {
        ledger::account::setNodeAddressTableMode(mode);
    }
    ~ScopedNodeAddressTableMode() noexcept
    {
        ledger::account::setNodeAddressTableMode(m_previous);
    }

    ScopedNodeAddressTableMode(ScopedNodeAddressTableMode const&) = delete;
    ScopedNodeAddressTableMode(ScopedNodeAddressTableMode&&) = delete;
    ScopedNodeAddressTableMode& operator=(ScopedNodeAddressTableMode const&) = delete;
    ScopedNodeAddressTableMode& operator=(ScopedNodeAddressTableMode&&) = delete;

private:
    ledger::account::AddressTableMode m_previous;
};
}  // namespace bcos::test
