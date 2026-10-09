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
 * @file LogFiles.cpp
 */
#include "LogFiles.h"
#include "bcos-ops/OpsError.h"
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace bcos::ops
{
namespace
{
bool allDigits(std::string_view _s)
{
    if (_s.empty())
    {
        return false;
    }
    for (char c : _s)
    {
        if (c < '0' || c > '9')
        {
            return false;
        }
    }
    return true;
}

std::optional<int64_t> toMs(std::string_view _ymd, std::string_view _hh, std::string_view _mm)
{
    if (_ymd.size() != 8 || _hh.size() != 2 || _mm.size() != 2 || !allDigits(_ymd) ||
        !allDigits(_hh) || !allDigits(_mm))
    {
        return std::nullopt;
    }
    std::tm tm{};
    tm.tm_year = std::stoi(std::string(_ymd.substr(0, 4))) - 1900;
    tm.tm_mon = std::stoi(std::string(_ymd.substr(4, 2))) - 1;
    tm.tm_mday = std::stoi(std::string(_ymd.substr(6, 2)));
    tm.tm_hour = std::stoi(std::string(_hh));
    tm.tm_min = std::stoi(std::string(_mm));
    tm.tm_isdst = -1;
    auto seconds = std::mktime(&tm);
    if (seconds < 0)
    {
        return std::nullopt;
    }
    return static_cast<int64_t>(seconds) * 1000;
}
}  // namespace

std::optional<int64_t> logFileStartMs(std::string const& _fileName)
{
    std::string_view name(_fileName);
    if (!name.starts_with("log_") || !name.ends_with(".log"))
    {
        return std::nullopt;
    }
    auto body = name.substr(4, name.size() - 8);  // between "log_" and ".log"
    // log_%Y%m%d_%H%M.log → 20261010_0144
    if (body.size() == 13 && body[8] == '_')
    {
        return toMs(body.substr(0, 8), body.substr(9, 2), body.substr(11, 2));
    }
    // log_%Y%m%d%H.%M.log → 2026101001.44
    if (body.size() == 13 && body[10] == '.')
    {
        return toMs(body.substr(0, 8), body.substr(8, 2), body.substr(11, 2));
    }
    return std::nullopt;
}

std::vector<std::string> selectLogFiles(std::string const& _dir, int64_t _sinceMs, int64_t _nowMs)
{
    std::vector<std::pair<int64_t, std::string>> candidates;
    std::error_code ec;
    for (auto const& entry : std::filesystem::directory_iterator(_dir, ec))
    {
        if (!entry.is_regular_file())
        {
            continue;
        }
        auto start = logFileStartMs(entry.path().filename().string());
        if (start)
        {
            candidates.emplace_back(*start, entry.path().string());
        }
    }
    std::sort(candidates.begin(), candidates.end());
    auto boundary = _nowMs - _sinceMs;
    std::vector<std::string> selected;
    std::optional<std::string> before;  // the newest file that started before the boundary
    for (auto const& [start, path] : candidates)
    {
        if (start < boundary)
        {
            before = path;
        }
        else
        {
            selected.push_back(path);
        }
    }
    if (before)
    {
        selected.insert(selected.begin(), *before);
    }
    return selected;
}

void forEachEvent(
    std::vector<std::string> const& _files, std::function<void(Event const&)> const& _fn)
{
    for (auto const& file : _files)
    {
        std::ifstream in(file);
        std::string line;
        while (std::getline(in, line))
        {
            if (auto event = parseLine(line))
            {
                _fn(*event);
            }
        }
    }
}

std::vector<Event> readEvents(std::vector<std::string> const& _files)
{
    std::vector<Event> events;
    forEachEvent(_files, [&events](Event const& _event) { events.push_back(_event); });
    return events;
}

int64_t parseDurationMs(std::string const& _text)
{
    if (_text.empty())
    {
        throw OpsError(c_exitUsage, "empty duration");
    }
    char unit = _text.back();
    std::string number = _text;
    int64_t factor = 1000;
    if (unit == 's' || unit == 'm' || unit == 'h' || unit == 'd')
    {
        number.pop_back();
        factor = unit == 's' ? 1000 : unit == 'm' ? 60000 : unit == 'h' ? 3600000 : 86400000;
    }
    else if (unit < '0' || unit > '9')
    {
        throw OpsError(c_exitUsage, "bad duration (use 90s, 30m, 1h, 2d): " + _text);
    }
    if (!allDigits(number))
    {
        throw OpsError(c_exitUsage, "bad duration (use 90s, 30m, 1h, 2d): " + _text);
    }
    return std::stoll(number) * factor;
}
}  // namespace bcos::ops
