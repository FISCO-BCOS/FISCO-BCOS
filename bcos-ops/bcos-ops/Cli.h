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
 * @brief subcommand table and dispatch for `fisco-bcos <subcommand> ...`
 * @file Cli.h
 */
#pragma once

#include "Args.h"
#include <functional>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

namespace bcos::ops
{
struct Command
{
    std::string summary;
    std::string usage;
    std::set<std::string> booleanFlags;
    std::function<int(Args const&, std::ostream& out, std::ostream& err)> run;
};

/// the registry, filled once on first use by the explicit register*Command functions below
/// (bcos-ops is a static library: a static initializer in an unreferenced object file would not
/// be linked into fisco-bcos)
std::map<std::string, Command>& commandTable();
void registerCommand(std::string _name, Command _command);
void registerStatusCommand();   // cmd/StatusCmd.cpp
void registerTxCommand();       // cmd/TxCmd.cpp
void registerAttachCommands();  // cmd/AttachCmd.cpp: attach, log-level
void registerLogCommand();      // cmd/LogCmd.cpp
void registerTuiCommand();      // cmd/TuiCmd.cpp

/// argv[0] is the program, argv[1] the subcommand. Returns the process exit code.
int runOps(int argc, const char* argv[]);
int runOps(std::vector<std::string> const& _args, std::ostream& _out, std::ostream& _err);

void printUsage(std::ostream& _out);
}  // namespace bcos::ops
