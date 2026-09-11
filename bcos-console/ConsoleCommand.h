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
 * @brief: console command base types
 * @file: ConsoleCommand.h
 */

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace bcos::console
{

// Callback type for command implementations.
// Parameters: the list of command arguments, the current BFS working directory.
// Returns: true on success, false on error (error already printed by callback).
using CommandCallback = std::function<bool(std::vector<std::string> const& params, std::string& pwd)>;

// Metadata describing a single registered command.
struct CommandInfo
{
    std::string name;                          // primary command name (e.g. "getBlockNumber")
    std::vector<std::string> aliases;          // alternative names (e.g. {"getblocknumber"})
    int minParams = 0;                         // minimum allowed positional params (excluding name)
    int maxParams = -1;                        // maximum allowed positional params (-1 = unlimited)
    bool needGroup = true;                     // whether this command requires a connected group
    bool needAuthOpen = false;                 // whether this command requires governance mode
    bool isWasmSupport = true;                 // whether the command is applicable to wasm groups
    std::string help;                          // help text (parameters + description)
    CommandCallback callback;                  // implementation
};

// Category grouping for help display.
struct CommandCategory
{
    std::string name;
    std::vector<CommandInfo> commands;
};

}  // namespace bcos::console
