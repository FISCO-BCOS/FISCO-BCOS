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
 * @file Log.cpp
 */
#include "Pages.h"
#include "bcos-ops/Output.h"
#include "bcos-ops/tui/LogTail.h"

namespace bcos::ops::tui
{
using namespace ftxui;

Element renderLog(Model const& _model, std::string const& _filter)
{
    Elements lines;
    for (auto const& event : _model.logTail)
    {
        if (!LogTail::matches(event, _filter))
        {
            continue;
        }
        std::string badges;
        for (auto const& badge : event.badges)
        {
            badges += "[" + badge + "]";
        }
        // the key/value tail: whatever follows the event name in the raw message (decorators
        // and [blk-N] badges make the prefix length unknowable from the parsed parts)
        auto namePos = event.name.empty() ? std::string::npos : event.raw.find(event.name);
        auto rest = namePos == std::string::npos ? std::string() :
                                                   event.raw.substr(namePos + event.name.size());
        auto line = hbox(
            {text(sanitizeForTerminal(
                 event.timestamp.size() > 11 ? event.timestamp.substr(11, 12) : event.timestamp)) |
                    dim,
                text(" " + sanitizeForTerminal(badges)) | dim,
                text(sanitizeForTerminal(event.name)) | bold, text(sanitizeForTerminal(rest))});
        if (!_filter.empty())
        {
            line = line | color(Color::Yellow);
        }
        else if (event.level == "warning" || event.level == "error")
        {
            line = line | color(Color::Red);
        }
        lines.push_back(line);
    }
    if (lines.empty())
    {
        lines.push_back(
            text(_filter.empty() ? "waiting for log lines" : "no line matches '" + _filter + "'") |
            dim);
    }
    // yframe scrolls to the focused child and sits on the first screen without one: focus the
    // newest line so the page tails the log
    lines.back() = lines.back() | focus;
    return vbox(std::move(lines)) | vscroll_indicator | yframe | flex;
}
}  // namespace bcos::ops::tui
