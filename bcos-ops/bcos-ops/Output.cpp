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
 * @file Output.cpp
 */
#include "Output.h"
#include <unistd.h>
#include <algorithm>
#include <iomanip>

namespace bcos::ops
{
bool stdoutIsTty()
{
    return ::isatty(STDOUT_FILENO) == 1;
}

bool wantJson(bool _jsonFlag)
{
    return _jsonFlag || !stdoutIsTty();
}

void printJson(std::ostream& _out, Json::Value const& _value)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    _out << Json::writeString(builder, _value) << '\n';
}

std::string sanitizeForTerminal(std::string const& _text)
{
    std::string out;
    out.reserve(_text.size());
    for (unsigned char c : _text)
    {
        if ((c < 0x20 && c != '\t') || c == 0x7f)
        {
            continue;
        }
        out.push_back(static_cast<char>(c));
    }
    return out;
}

void printRows(std::ostream& _out, std::vector<std::pair<std::string, std::string>> const& _rows)
{
    size_t width = 0;
    for (auto const& [label, _] : _rows)
    {
        width = std::max(width, label.size());
    }
    for (auto const& [label, value] : _rows)
    {
        _out << std::left << std::setw(static_cast<int>(width) + 2) << label
             << sanitizeForTerminal(value) << '\n';
    }
}

std::string abridged(std::string const& _hex, size_t _keep)
{
    auto body = _hex;
    if (body.starts_with("0x") || body.starts_with("0X"))
    {
        body = body.substr(2);
    }
    if (body.size() <= 2 * _keep)
    {
        return _hex;
    }
    return body.substr(0, _keep) + "…" + body.substr(body.size() - _keep);
}
}  // namespace bcos::ops
