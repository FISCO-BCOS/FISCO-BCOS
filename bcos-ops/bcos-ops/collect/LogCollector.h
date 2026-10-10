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
 * @brief NodeStatus from the log alone (source=log): what the last Report, the sync flips and the
 *        last TXPOOL BlockStat say; everything else null with a reason
 * @file LogCollector.h
 */
#pragma once

#include "bcos-ops/NodeStatus.h"
#include "bcos-ops/log/LogParser.h"
#include <vector>

namespace bcos::ops
{
/// _viewChangeWindowMs: a ViewChangeTriggered newer than _nowMs - window marks inTimeout
NodeStatus collectFromLog(
    std::vector<Event> const& _events, int64_t _nowMs, int64_t _viewChangeWindowMs);
}  // namespace bcos::ops
