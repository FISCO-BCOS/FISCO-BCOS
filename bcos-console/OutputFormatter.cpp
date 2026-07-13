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
 * @brief: output formatting implementation
 * @file: OutputFormatter.cpp
 */

#include "OutputFormatter.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <sstream>

using namespace bcos::console;

void OutputFormatter::printWelcome(std::string_view version)
{
    auto out = [](std::string_view s) { std::cout << s << '\n'; };

    out(R"(  ______  _____  _____  _____       _____   ______  _____)");
    out(R"( |  ____|/ ____|/ ____||_   _|     |  _  \ /  ____|/ ____|)");
    out(R"( | |__  | (___ | |       | | ___   | |_|  || |     | (___)");
    out(R"( |  __|  \___ \| |       | |/ _ \  |  _  / | |      \___ \)");
    out(R"( | |     ____) || |____   | | (_) | | | \ \ | |____  ____) |)");
    out(R"( |_|    |_____/ \_____|  |_|\___/  |_|  \_\ \_____||_____/)");
    out("");
    std::cout << "Welcome to FISCO BCOS console(" << version << ")!" << '\n';
    std::cout << "Type 'help' or 'h' for help. Type 'quit' or 'q' to quit console." << '\n';
    std::cout << '\n';
}

void OutputFormatter::printSingleLine()
{
    std::cout << std::string(SEPARATOR_WIDTH, '-') << '\n';
}

void OutputFormatter::printDoubleLine()
{
    std::cout << std::string(SEPARATOR_WIDTH, '=') << '\n';
}

void OutputFormatter::printBlankLine()
{
    std::cout << '\n';
}

void OutputFormatter::printJson(Json::Value const& value)
{
    printJson(value, [](std::string_view s) { std::cout << s; });
}

void OutputFormatter::printJson(
    Json::Value const& value, std::function<void(std::string_view)> writer)
{
    auto out = std::ref(writer);
    printJsonInternal(value, 0, out);
    writer("\n");
}

void OutputFormatter::printJsonInternal(
    Json::Value const& value, int indent, std::function<void(std::string_view)>& writer)
{
    auto ind = std::string(indent * TAB_WIDTH, ' ');

    if (value.isObject())
    {
        writer("{\n");
        auto keys = value.getMemberNames();
        for (size_t i = 0; i < keys.size(); ++i)
        {
            auto const& key = keys[i];
            writer(ind + "    \"" + key + "\": ");
            printJsonInternal(value[key], indent + 1, writer);
            if (i < keys.size() - 1)
            {
                writer(",\n");
            }
            else
            {
                writer("\n");
            }
        }
        writer(ind + "}");
    }
    else if (value.isArray())
    {
        if (value.empty())
        {
            writer("[]");
            return;
        }
        writer("[\n");
        for (Json::ArrayIndex i = 0; i < value.size(); ++i)
        {
            writer(ind + "    ");
            printJsonInternal(value[i], indent + 1, writer);
            if (i < value.size() - 1)
            {
                writer(",\n");
            }
            else
            {
                writer("\n");
            }
        }
        writer(ind + "]");
    }
    else if (value.isString())
    {
        writer("\"" + value.asString() + "\"");
    }
    else if (value.isBool())
    {
        writer(value.asBool() ? "true" : "false");
    }
    else if (value.isNull())
    {
        writer("null");
    }
    else if (value.isInt64())
    {
        writer(std::to_string(value.asInt64()));
    }
    else if (value.isUInt64())
    {
        writer(std::to_string(value.asUInt64()));
    }
    else if (value.isDouble())
    {
        writer(value.asString());  // preserve original format
    }
    else if (value.isInt())
    {
        writer(std::to_string(value.asInt()));
    }
    else if (value.isUInt())
    {
        writer(std::to_string(value.asUInt()));
    }
    else
    {
        auto str = value.asString();
        writer(str);
    }
}

std::string OutputFormatter::buildPrompt(std::string_view groupID, std::string_view pwd)
{
    return "[" + std::string(groupID) + "]: " + std::string(pwd) + "> ";
}

void OutputFormatter::printTable(std::vector<std::string> const& headers,
    std::vector<std::vector<std::string>> const& rows,
    std::function<void(std::string_view)> writer)
{
    if (headers.empty())
    {
        return;
    }

    // Calculate column widths based on headers + all rows
    std::vector<size_t> widths(headers.size(), 0);
    for (size_t i = 0; i < headers.size(); ++i)
    {
        widths[i] = headers[i].size();
    }
    for (auto const& row : rows)
    {
        for (size_t i = 0; i < std::min(row.size(), headers.size()); ++i)
        {
            widths[i] = std::max(widths[i], row[i].size());
        }
    }

    // Build separator line: +---+---+---+
    std::string sep = "+";
    for (auto w : widths)
    {
        sep += std::string(w + 2, '-') + "+";
    }
    sep += "\n";

    // Top border
    writer(sep);

    // Header row
    writer("|");
    for (size_t i = 0; i < headers.size(); ++i)
    {
        writer(" " + headers[i] + std::string(widths[i] - headers[i].size() + 1, ' ') + "|");
    }
    writer("\n");

    // Header/data separator
    writer(sep);

    // Data rows
    for (auto const& row : rows)
    {
        writer("|");
        for (size_t i = 0; i < headers.size(); ++i)
        {
            auto cell = i < row.size() ? row[i] : std::string();
            writer(" " + cell + std::string(widths[i] - cell.size() + 1, ' ') + "|");
        }
        writer("\n");
    }

    // Bottom border
    writer(sep);
}

void OutputFormatter::printTable(
    std::vector<std::string> const& headers, std::vector<std::vector<std::string>> const& rows)
{
    printTable(headers, rows, [](std::string_view s) { std::cout << s; });
}

std::string OutputFormatter::formatError(int64_t code, std::string_view message)
{
    Json::Value error;
    error["code"] = code;
    error["msg"] = std::string(message);
    std::ostringstream oss;
    printJson(error, [&oss](std::string_view s) { oss << s; });
    return oss.str();
}
