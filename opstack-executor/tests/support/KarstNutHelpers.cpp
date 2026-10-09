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

#include "KarstNutHelpers.h"

#include <string>

namespace opstack_test
{
bcos::ledger::OpForkSchedule karstOnlySchedule(uint64_t karstTs)
{
    // jovian implied at karst's second (the fold rule): jovian_time = karst_time.
    bcos::ledger::OpForkSchedule schedule;
    schedule.m_jovianTime = karstTs;
    schedule.m_karstTime = karstTs;
    return schedule;
}

bcos::ledger::OpForkSchedule isthmusThenJovian(uint64_t jovianTs)
{
    bcos::ledger::OpForkSchedule schedule;
    schedule.m_jovianTime = jovianTs;
    return schedule;
}
}  // namespace opstack_test
