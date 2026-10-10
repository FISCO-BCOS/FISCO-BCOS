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
 * @file Cli.cpp
 */
#include "Cli.h"
#include "OpsError.h"
#include <boost/log/core.hpp>
#include <cstdlib>
#include <iostream>

namespace bcos::ops
{
namespace
{
std::map<std::string, Command>& rawTable()
{
    static std::map<std::string, Command> table;
    return table;
}
}  // namespace

std::map<std::string, Command>& commandTable()
{
    // the register functions write through registerCommand → rawTable(), so this one-time
    // initializer never re-enters commandTable()
    static bool const registered = []() {
        registerStatusCommand();
        registerTxCommand();
        registerAttachCommands();
        registerLogCommand();
        return true;
    }();
    (void)registered;
    return rawTable();
}

void registerCommand(std::string _name, Command _command)
{
    rawTable()[std::move(_name)] = std::move(_command);
}

void printUsage(std::ostream& _out)
{
    _out << "usage: fisco-bcos <subcommand> [options]\n\n"
         << "Without a subcommand the binary starts the node (see fisco-bcos --help).\n\n"
         << "subcommands:\n";
    for (auto const& [name, command] : commandTable())
    {
        _out << "  " << name;
        for (size_t i = name.size(); i < 12; ++i)
        {
            _out << ' ';
        }
        _out << command.summary << '\n';
    }
    _out << "\ncommon options:\n"
         << "  --node-dir <dir>   node directory holding config.ini (default: .)\n"
         << "  --rpc <host:port>  connect to a plaintext RPC port instead of --node-dir\n"
         << "  --json             force JSON output (default when stdout is not a TTY)\n"
         << "\nexit codes: 0 ok; 1 usage or connection error; 2 node reachable but a check "
            "failed\n";
}

int runOps(std::vector<std::string> const& _args, std::ostream& _out, std::ostream& _err)
{
    if (_args.size() < 2)
    {
        printUsage(_err);
        return c_exitUsage;
    }
    auto const& name = _args[1];
    if (name == "help" || name == "--help" || name == "-h")
    {
        printUsage(_out);
        return c_exitOk;
    }
    auto it = commandTable().find(name);
    if (it == commandTable().end())
    {
        _err << "unknown subcommand: " << name << "\n\n";
        printUsage(_err);
        return c_exitUsage;
    }
    auto const& command = it->second;
    auto booleanFlags = command.booleanFlags;
    booleanFlags.insert({"json", "help", "h"});
    Args args(std::vector<std::string>(_args.begin() + 2, _args.end()), booleanFlags);
    if (args.help())
    {
        _out << "usage: fisco-bcos " << name << ' ' << command.usage << "\n\n"
             << command.summary << '\n';
        return c_exitOk;
    }
    try
    {
        return command.run(args, _out, _err);
    }
    catch (OpsError const& e)
    {
        _err << "error: " << e.what() << '\n';
        return e.exitCode;
    }
    catch (std::exception const& e)
    {
        _err << "error: " << e.what() << '\n';
        return c_exitUsage;
    }
}

int runOps(int argc, const char* argv[])
{
    // The CLI never initialises a log sink, and Boost.Log prints records to the console when no
    // sink is registered; the SDK's ABI codec logs at TRACE. Silence the core so command output
    // stays machine-readable.
    boost::log::core::get()->set_logging_enabled(false);
    std::vector<std::string> args;
    args.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i)
    {
        args.emplace_back(argv[i]);
    }
    auto code = runOps(args, std::cout, std::cerr);
    std::cout.flush();
    std::cerr.flush();
    // see retainTransport(): the SDK transports stay alive and the process leaves without
    // running their destructors
    std::_Exit(code);
}
}  // namespace bcos::ops
