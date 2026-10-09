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
 * @brief follows the newest log file: each poll parses the lines appended since the last one
 * @file LogTail.h
 */
#pragma once

#include "bcos-ops/log/LogParser.h"
#include <cstdint>
#include <string>
#include <vector>

namespace bcos::ops::tui
{
class LogTail
{
public:
    explicit LogTail(std::string _logDir) : m_logDir(std::move(_logDir)) {}
    /// new events since the previous poll; switches to a newer file when one appears
    std::vector<Event> poll();
    /// true when the event's name or any badge contains _filter (empty filter matches all)
    static bool matches(Event const& _event, std::string const& _filter);
    std::string const& currentFile() const { return m_file; }

private:
    std::string m_logDir;
    std::string m_file;
    std::streamoff m_offset = 0;
};
}  // namespace bcos::ops::tui
