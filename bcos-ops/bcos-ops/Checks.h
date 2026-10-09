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
 * @brief the five single-sample health checks; a check whose inputs are null is `skipped`
 * @file Checks.h
 */
#pragma once

#include "NodeStatus.h"
#include <json/json.h>
#include <string>
#include <vector>

namespace bcos::ops
{
struct Thresholds
{
    double stallFactor =
        2.0;              // --stall-factor: height stalls when now - latestTimestamp > f × timeout
    int64_t maxLag = 10;  // --max-lag
    int64_t viewChangeWindowMs = 60000;  // log source: a ViewChangeTriggered within this window
};

struct Check
{
    std::string name;
    std::string state;  // "ok" | "fail" | "skipped"
    std::string detail;
    Json::Value values{Json::objectValue};
    bool ok() const { return state == "ok"; }
    bool failed() const { return state == "fail"; }
};

std::vector<Check> evaluate(
    NodeStatus const& _status, Thresholds const& _thresholds, int64_t _nowMs);
bool anyFailed(std::vector<Check> const& _checks);
Json::Value checksToJson(std::vector<Check> const& _checks);
}  // namespace bcos::ops
