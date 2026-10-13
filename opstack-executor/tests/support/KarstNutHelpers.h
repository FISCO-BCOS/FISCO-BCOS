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
 * @file KarstNutHelpers.h
 * @brief Test helpers for the Karst-activation suites: schedule builders and deposit fixtures. */

#pragma once

// Test-only Karst schedule builders (the two schedule constructors below).
// Production parse names `karst` after a Jovian baseline or activation; the
// historical TestBypass shape retired with the codec-locked fixtures. Do not
// call from Initializer (review F38).

#include <bcos-framework/ledger/OpForkSchedule.h>
#include <cstdint>

namespace opstack_test
{
[[nodiscard]] bcos::ledger::OpForkSchedule karstOnlySchedule(uint64_t karstTs);

[[nodiscard]] bcos::ledger::OpForkSchedule isthmusThenJovian(uint64_t jovianTs);

}  // namespace opstack_test
