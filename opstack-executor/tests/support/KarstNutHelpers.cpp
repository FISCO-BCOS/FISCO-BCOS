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
using bcos::evm::opstack::OpForkSchedule;

std::shared_ptr<OpForkSchedule> karstOnlySchedule(uint64_t karstTs)
{
    return std::make_shared<OpForkSchedule>(
        OpForkSchedule::parse("0:jovian," + std::to_string(karstTs) + ":karst"));
}

std::shared_ptr<OpForkSchedule> isthmusThenJovian(uint64_t jovianTs)
{
    return std::make_shared<OpForkSchedule>(
        OpForkSchedule::parse("0:isthmus," + std::to_string(jovianTs) + ":jovian"));
}

std::shared_ptr<OpForkSchedule> legacySchedule(bool jovianActive)
{
    return std::make_shared<OpForkSchedule>(OpForkSchedule::legacy(jovianActive));
}
}  // namespace opstack_test
