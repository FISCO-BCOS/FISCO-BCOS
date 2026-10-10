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
 * @brief everything the panel shows; written by the refresher / log tail threads, read by the
 *        renderers under the mutex. No fetching or judging logic lives in the tui/ directory.
 * @file Model.h
 */
#pragma once

#include "bcos-ops/Checks.h"
#include "bcos-ops/NodeStatus.h"
#include "bcos-ops/log/LogParser.h"
#include "bcos-ops/tx/Smoke.h"
#include <json/json.h>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace bcos::ops::tui
{
struct Model
{
    std::mutex mutex;
    NodeStatus status;
    std::vector<Check> checks;
    Json::Value consensus;  // getConsensusStatus (unwrapped)
    Json::Value sync;       // getSyncStatus (unwrapped)
    Json::Value peers;      // getPeers
    std::deque<Event> logTail;
    std::string source;
    std::string error;  // last refresh failure; empty after a success
    int64_t lastRefreshMs = 0;
    int refreshInSeconds = 0;
    // smoke
    bool smokeRunning = false;
    std::optional<SmokeResult> smoke;
    std::string smokeError;
};
}  // namespace bcos::ops::tui
