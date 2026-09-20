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
 * @brief: output formatting utilities
 * @file: OutputFormatter.h
 */

#pragma once

#include <json/json.h>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::console
{

// Output formatting utilities matching the Java console style.
class OutputFormatter
{
public:
    // Print the welcome banner (ASCII art logo + version).
    static void printWelcome(std::string_view version);

    // Print a single-line separator: ----...---- (93 dashes).
    static void printSingleLine();

    // Print a double-line separator: ====...==== (93 equals).
    static void printDoubleLine();

    // Print a blank line.
    static void printBlankLine();

    // Pretty-print a JSON value with 4-space indentation (matching Java Console style).
    static void printJson(Json::Value const& value, std::function<void(std::string_view)> writer);

    // Pretty-print to stdout.
    static void printJson(Json::Value const& value);

    // Build the BFS-style prompt string: [groupID]: /path>
    static std::string buildPrompt(std::string_view groupID, std::string_view pwd);

    // Print a formatted ASCII table. Column widths are auto-sized.
    //  headers: column names; rows: data rows (each row is a vector of strings).
    static void printTable(std::vector<std::string> const& headers,
        std::vector<std::vector<std::string>> const& rows,
        std::function<void(std::string_view)> writer);

    // Print table to stdout.
    static void printTable(std::vector<std::string> const& headers,
        std::vector<std::vector<std::string>> const& rows);

    // Format a JSON-RPC error response for display.
    static std::string formatError(int64_t code, std::string_view message);

private:
    static void printJsonInternal(
        Json::Value const& value, int indent, std::function<void(std::string_view)>& writer);

    static constexpr int TAB_WIDTH = 4;
    static constexpr int SEPARATOR_WIDTH = 93;
};

}  // namespace bcos::console
