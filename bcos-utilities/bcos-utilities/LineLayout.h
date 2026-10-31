/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @brief: compiled form of a custom log.format string. The whole-line
 *  pipeline assembles log lines on the producer thread without building a
 *  boost record, so a custom format is compiled once at initLog into a
 *  segment list that commitLine replays directly; when no format is
 *  configured the built-in layout fast path is used unchanged.
 *
 * @file: LineLayout.h
 */
#pragma once

#include "LogStream.h"
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::log
{
// A compiled log.format. Owns every string its segments reference, so the
// installed instance is simply leaked (same lifetime contract as the
// line-sink registry and LineFeeder).
class LineLayout
{
public:
    enum class Field : uint8_t
    {
        Literal,
        Severity,
        TimeStamp,
        ThreadName,
        ThreadID,
        LineID,
        ProcessID,
        Message
    };
    // One piece of a TimeStamp format: either a literal run or a fixed-width
    // numeric field. Rendering needs no per-line allocation or locale.
    struct TimePart
    {
        enum class Kind : uint8_t
        {
            Literal,
            Year4,
            Year2,
            Month,
            Day,
            Hour,
            Minute,
            Second,
            Frac6  // %f: microseconds, always 6 digits
        };
        Kind kind = Kind::Literal;
        std::string literal;
    };
    struct Segment
    {
        Field field = Field::Literal;
        std::string literal;              // Field::Literal text
        std::vector<TimePart> timeParts;  // Field::TimeStamp compiled format
    };

    std::vector<Segment> segments;
    bool usesTimeStamp = false;  // any TimeStamp segment present (clock read is skipped otherwise)
    bool usesLineID = false;     // any LineID segment present (id is incremented only then)
    std::string processID;       // decimal pid, rendered once at parse time
};

// Parses the subset of the legacy boost::log formatter grammar that the
// record-based pipeline accepted: literal text (%% is an escaped %) plus
// %Severity%, %TimeStamp(format="...")%, %ThreadName%, %ThreadID%, %LineID%,
// %ProcessID% and %Message%. The TimeStamp format supports the boost
// date_time flags %Y %y %m %d %H %M %S %f %%; without a format argument it
// defaults to "%Y-%m-%d %H:%M:%S.%f" (to_simple_string at microsecond
// resolution). Returns nullptr on unsupported syntax with the reason in
// _error.
std::unique_ptr<LineLayout> parseLineLayout(std::string_view _format, std::string& _error);

// Installs the layout commitLine renders with; nullptr restores the built-in
// layout. Written at initLog (cold path); the object is never freed.
void setLineLayout(std::unique_ptr<LineLayout> _layout);

// The installed layout, or nullptr for the built-in layout. One atomic load;
// called once per committed log line.
LineLayout const* lineLayout() noexcept;

// Renders one complete line (message included) per the layout.
void appendLayoutLine(
    std::string& _out, LineLayout const& _layout, LogLevel _level, std::string_view _message);

// One complete line with the custom layout when installed, built-in
// otherwise; used for internally generated marker lines so they follow the
// same layout as producer-formatted lines.
std::string formatLogLine(LogLevel _level, std::string_view _message);
}  // namespace bcos::log
