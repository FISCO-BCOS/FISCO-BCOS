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
 */

#pragma once

#include <bcos-evm/opstack/OpForkSchedule.h>

namespace bcos::evm::opstack
{
/// Test-only Karst schedule via `TestBypass`. Production `parse("…:karst")` is
/// valid after Jovian; this helper still skips codec validation.
/// Lives in `opstack` (not a nested `::test`) to avoid colliding with `evmone::test`
/// under the unity-build `using namespace bcos::evm::opstack`.
inline OpForkSchedule karstOnly()
{
    return OpForkSchedule{{{OpFork::Isthmus, 0}, {OpFork::Jovian, 1}, {OpFork::Karst, 2}},
        OpForkSchedule::TestBypass{}};
}
}  // namespace bcos::evm::opstack
