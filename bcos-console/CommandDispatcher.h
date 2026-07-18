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
 * @brief: command registration and routing
 * @file: CommandDispatcher.h
 */

#pragma once

#include "ConsoleCommand.h"
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::console
{

class CommandDispatcher
{
public:
    CommandDispatcher() = default;

    // Register a single command. Aliases are automatically registered.
    void addCommand(CommandInfo info);

    // Register a category of commands for help grouping.
    void addCategory(CommandCategory category);

    // Look up a command by name (case-insensitive).
    // Returns nullptr if no match is found.
    CommandInfo const* findCommand(std::string_view name) const;

    // Return all registered categories (for help display).
    std::vector<CommandCategory> const& categories() const { return m_categories; }

    // Return completion candidates matching a prefix.
    std::vector<std::string> completions(std::string_view prefix) const;

    // Check whether a command name exists (case-insensitive).
    bool hasCommand(std::string_view name) const;

private:
    // primary name → index into m_commands
    std::map<std::string, size_t, std::less<>> m_nameIndex;
    // all command infos (owned here)
    std::vector<CommandInfo> m_commands;
    // category groupings for --help
    std::vector<CommandCategory> m_categories;
};

}  // namespace bcos::console
