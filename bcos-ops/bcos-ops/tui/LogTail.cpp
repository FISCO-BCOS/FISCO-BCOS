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
 * @file LogTail.cpp
 */
#include "LogTail.h"
#include "bcos-ops/log/LogFiles.h"
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace bcos::ops::tui
{
namespace
{
constexpr size_t c_maxLineBytes = 64 * 1024;
constexpr size_t c_maxEventsPerPoll = 2000;
}  // namespace

std::vector<Event> LogTail::poll()
{
    std::vector<Event> events;
    // newest file by the timestamp in its name
    std::string newest;
    int64_t newestStart = -1;
    std::error_code ec;
    for (auto const& entry : std::filesystem::directory_iterator(m_logDir, ec))
    {
        auto start = logFileStartMs(entry.path().filename().string());
        if (start && *start > newestStart)
        {
            newestStart = *start;
            newest = entry.path().string();
        }
    }
    if (newest.empty())
    {
        return events;
    }
    if (newest != m_file)
    {
        m_file = newest;
        m_offset = 0;
        m_partial.clear();
    }
    std::ifstream in(m_file);
    if (!in)
    {
        return events;
    }
    in.seekg(0, std::ios::end);
    auto size = static_cast<std::streamoff>(in.tellg());
    if (size < m_offset)
    {
        m_offset = 0;  // truncated / rotated in place
        m_partial.clear();
    }
    if (m_offset == 0 && size > 0 && m_partial.empty() && events.empty() && m_file == newest &&
        size > 4 * 1024 * 1024)
    {
        // first look at a big file: start from the last 4 MB rather than the whole history
        m_offset = size - 4 * 1024 * 1024;
        in.seekg(m_offset);
        std::string skipped;
        std::getline(in, skipped);  // drop the partial line we landed in
        m_offset = static_cast<std::streamoff>(in.tellg());
    }
    in.seekg(m_offset);
    std::string line;
    while (std::getline(in, line))
    {
        if (in.eof() && !line.empty() && in.peek() == std::char_traits<char>::eof())
        {
            // no trailing newline yet: keep for the next poll, but never more than one log line's
            // worth (a file that stops mid-line forever must not grow memory)
            if (m_partial.size() + line.size() <= c_maxLineBytes)
            {
                m_partial += line;
            }
            else
            {
                m_partial.clear();
                m_offset = size;
            }
            break;
        }
        if (events.size() >= c_maxEventsPerPoll)
        {
            // a huge backlog is read across several polls; the offset is already past this line
            m_offset = static_cast<std::streamoff>(in.tellg());
            break;
        }
        auto full = m_partial + line;
        m_partial.clear();
        if (auto event = parseLine(full))
        {
            events.push_back(*event);
        }
        m_offset = static_cast<std::streamoff>(in.tellg());
        if (m_offset < 0)
        {
            m_offset = size;
        }
    }
    return events;
}

bool LogTail::matches(Event const& _event, std::string const& _filter)
{
    if (_filter.empty())
    {
        return true;
    }
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    };
    auto needle = lower(_filter);
    if (lower(_event.name).find(needle) != std::string::npos)
    {
        return true;
    }
    for (auto const& badge : _event.badges)
    {
        if (lower(badge).find(needle) != std::string::npos)
        {
            return true;
        }
    }
    return false;
}
}  // namespace bcos::ops::tui
