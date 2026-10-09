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
 * @file LogParser.cpp
 */
#include "LogParser.h"
#include <ctime>

namespace bcos::ops
{
bool Event::hasBadge(std::string_view _badge) const
{
    for (auto const& badge : badges)
    {
        if (badge == _badge)
        {
            return true;
        }
    }
    return false;
}

std::string Event::get(std::string_view _key, std::string _default) const
{
    auto it = kv.find(std::string(_key));
    return it == kv.end() ? std::move(_default) : it->second;
}

std::optional<int64_t> Event::getInt(std::string_view _key) const
{
    auto it = kv.find(std::string(_key));
    if (it == kv.end())
    {
        return std::nullopt;
    }
    try
    {
        size_t used = 0;
        auto value = std::stoll(it->second, &used);
        return used > 0 ? std::optional<int64_t>(value) : std::nullopt;
    }
    catch (std::exception const&)
    {
        return std::nullopt;
    }
}

std::optional<int64_t> parseTimestampMs(std::string_view _text)
{
    // 2026-10-10 01:44:53.252140
    if (_text.size() < 19 || _text[4] != '-' || _text[7] != '-' || _text[10] != ' ' ||
        _text[13] != ':' || _text[16] != ':')
    {
        return std::nullopt;
    }
    auto num = [&](size_t _from, size_t _len) -> std::optional<int> {
        int value = 0;
        for (size_t i = _from; i < _from + _len; ++i)
        {
            if (i >= _text.size() || _text[i] < '0' || _text[i] > '9')
            {
                return std::nullopt;
            }
            value = value * 10 + (_text[i] - '0');
        }
        return value;
    };
    auto year = num(0, 4);
    auto month = num(5, 2);
    auto day = num(8, 2);
    auto hour = num(11, 2);
    auto minute = num(14, 2);
    auto second = num(17, 2);
    if (!year || !month || !day || !hour || !minute || !second)
    {
        return std::nullopt;
    }
    std::tm tm{};
    tm.tm_year = *year - 1900;
    tm.tm_mon = *month - 1;
    tm.tm_mday = *day;
    tm.tm_hour = *hour;
    tm.tm_min = *minute;
    tm.tm_sec = *second;
    tm.tm_isdst = -1;
    auto seconds = std::mktime(&tm);
    if (seconds < 0)
    {
        return std::nullopt;
    }
    int64_t millis = 0;
    if (_text.size() > 20 && _text[19] == '.')
    {
        int digits = 0;
        for (size_t i = 20; i < _text.size() && digits < 3; ++i, ++digits)
        {
            if (_text[i] < '0' || _text[i] > '9')
            {
                break;
            }
            millis = millis * 10 + (_text[i] - '0');
        }
        for (; digits < 3; ++digits)
        {
            millis *= 10;
        }
    }
    return static_cast<int64_t>(seconds) * 1000 + millis;
}

std::optional<Event> parseLine(std::string_view _line)
{
    auto first = _line.find('|');
    if (first == std::string_view::npos || first == 0 || first > 8)
    {
        return std::nullopt;
    }
    auto second = _line.find('|', first + 1);
    if (second == std::string_view::npos)
    {
        return std::nullopt;
    }
    auto third = _line.find('|', second + 1);
    if (third == std::string_view::npos)
    {
        return std::nullopt;
    }
    Event event;
    event.level = std::string(_line.substr(0, first));
    event.timestamp = std::string(_line.substr(first + 1, second - first - 1));
    auto timeMs = parseTimestampMs(event.timestamp);
    if (!timeMs)
    {
        return std::nullopt;
    }
    event.timeMs = *timeMs;
    auto message = _line.substr(third + 1);
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r'))
    {
        message.remove_suffix(1);
    }
    event.raw = std::string(message);

    size_t pos = 0;
    while (pos < message.size() && message[pos] == '[')
    {
        auto close = message.find(']', pos);
        if (close == std::string_view::npos)
        {
            break;
        }
        auto badge = message.substr(pos + 1, close - pos - 1);
        if (badge.starts_with("blk-"))
        {
            event.kv["blk"] = std::string(badge.substr(4));
        }
        else
        {
            event.badges.emplace_back(badge);
        }
        pos = close + 1;
    }
    // decorators: ^^^^^^^^Report, ++++++++++++++++ PrePrepareSent, ######## CommitQuorum
    while (pos < message.size() && (message[pos] == '^' || message[pos] == '+' ||
                                       message[pos] == '#' || message[pos] == ' '))
    {
        ++pos;
    }
    auto comma = message.find(',', pos);
    event.name = std::string(message.substr(
        pos, comma == std::string_view::npos ? std::string_view::npos : comma - pos));
    while (!event.name.empty() && event.name.back() == ' ')
    {
        event.name.pop_back();
    }
    if (comma != std::string_view::npos)
    {
        auto rest = message.substr(comma + 1);
        size_t start = 0;
        while (start <= rest.size())
        {
            auto end = rest.find(',', start);
            auto pair = rest.substr(
                start, end == std::string_view::npos ? std::string_view::npos : end - start);
            auto eq = pair.find('=');
            if (eq != std::string_view::npos && eq > 0)
            {
                event.kv.emplace(std::string(pair.substr(0, eq)), std::string(pair.substr(eq + 1)));
            }
            if (end == std::string_view::npos)
            {
                break;
            }
            start = end + 1;
        }
    }
    return event;
}
}  // namespace bcos::ops
