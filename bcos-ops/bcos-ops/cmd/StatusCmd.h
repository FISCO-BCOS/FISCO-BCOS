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
 * @brief `fisco-bcos status`; runStatusOn is the testable core that takes an open Connection
 * @file StatusCmd.h
 */
#pragma once

#include "bcos-ops/Checks.h"
#include "bcos-ops/NodeStatus.h"
#include "bcos-ops/RpcCall.h"
#include <iosfwd>

namespace bcos::ops
{
Thresholds thresholdsFrom(class Args const& _args);
void renderStatus(
    std::ostream& _out, NodeStatus const& _status, std::vector<Check> const& _checks, bool _json);
/// collect + evaluate + render; returns 0 or 2
int runStatusOn(
    Connection const& _connection, Thresholds const& _thresholds, bool _json, std::ostream& _out);
}  // namespace bcos::ops
