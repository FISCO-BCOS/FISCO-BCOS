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
 * @brief pure line parser for the default log format
 *        `Severity|%Y-%m-%d %H:%M:%S.%f|Thread|[badge]...EventName,k=v,...`
 * @file LogParser.h
 */
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::ops
{
struct Event
{
    std::string level;                      // info / debug / warning ...
    std::string timestamp;                  // as printed
    int64_t timeMs = 0;                     // local-time epoch ms
    std::vector<std::string> badges;        // ["CONSENSUS", "PBFT", "METRIC"], [blk-N] excluded
    std::string name;                       // event name with the decorators stripped
    std::map<std::string, std::string> kv;  // k=v pairs; [blk-N] becomes kv["blk"]
    std::string raw;                        // the message after the third '|'

    bool hasBadge(std::string_view _badge) const;
    std::string get(std::string_view _key, std::string _default = "") const;
    std::optional<int64_t> getInt(std::string_view _key) const;
};

/// nullopt when the line does not have the default four-field prefix (stack traces, blanks)
std::optional<Event> parseLine(std::string_view _line);
/// "%Y-%m-%d %H:%M:%S.%f" (local time) → epoch ms; nullopt when malformed
std::optional<int64_t> parseTimestampMs(std::string_view _text);
}  // namespace bcos::ops
