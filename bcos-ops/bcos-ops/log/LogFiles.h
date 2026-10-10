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
 * @brief picks the log files to read by the timestamp in their names and streams their events
 * @file LogFiles.h
 */
#pragma once

#include "LogParser.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::ops
{
/// start time encoded in `log_%Y%m%d_%H%M.log` or `log_%Y%m%d%H.%M.log`; nullopt otherwise
std::optional<int64_t> logFileStartMs(std::string const& _fileName);
/// files whose start >= _nowMs - _sinceMs, plus the one right before (it spans the boundary),
/// oldest first. Only names are inspected, never contents.
std::vector<std::string> selectLogFiles(std::string const& _dir, int64_t _sinceMs, int64_t _nowMs);
/// parses every line of the given files in order; unparsable lines are skipped
void forEachEvent(
    std::vector<std::string> const& _files, std::function<void(Event const&)> const& _fn);
std::vector<Event> readEvents(std::vector<std::string> const& _files);
/// streams the files and keeps only the events _keep accepts, so a DEBUG-level window does not
/// have to fit in memory as a whole
std::vector<Event> readEventsMatching(
    std::vector<std::string> const& _files, std::function<bool(Event const&)> const& _keep);
/// streams the files and keeps, per (event name, first badge), only the newest event _keep
/// accepts, in time order: a status snapshot over a long window costs a handful of events
std::vector<Event> readLastEventOfEach(
    std::vector<std::string> const& _files, std::function<bool(Event const&)> const& _keep);
/// a keep-predicate for "event name is one of"
std::function<bool(Event const&)> namedEvents(std::vector<std::string_view> _names);
/// "1h", "30m", "90s", "2d" → ms; throws OpsError{1} on a malformed value
int64_t parseDurationMs(std::string const& _text);
}  // namespace bcos::ops
