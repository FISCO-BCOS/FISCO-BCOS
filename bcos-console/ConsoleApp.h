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
 * @brief: main console application — REPL loop, command dispatch, output
 * @file: ConsoleApp.h
 */

#pragma once

#include "CommandDispatcher.h"
#include "ConsoleRepl.h"
#include "OutputFormatter.h"
#include "config/ConsoleConfig.h"
#include "connection/RpcConnection.h"
#include "connection/LocalRpcConnection.h"
#include "keymanager/KeyManager.h"

#include <memory>
#include <string>

namespace bcos::console
{

class ConsoleApp
{
public:
    ConsoleApp() = default;
    ~ConsoleApp() = default;

    // Initialize the console from a config file path.
    // If jsonRpc is provided (non-null), use local in-process mode.
    // Otherwise connect remotely via WebSocket using the config.
    bool init(std::string_view configPath,
        ::bcos::rpc::JsonRpcInterface::Ptr jsonRpc = nullptr,
        std::string_view defaultGroup = {});

    // Start the REPL loop. Blocks until user quits.
    void start();

    // Stop the console.
    void stop();

    // Set a custom prompt prefix.
    void setPromptPrefix(std::string_view prefix) { m_promptPrefix = prefix; }

    // Accessors
    std::string currentGroup() const { return m_currentGroup; }
    std::string currentPwd() const { return m_currentPwd; }
    KeyManager::Ptr keyManager() const { return m_keyManager; }

private:
    // Tokenize an input line into command + arguments.
    static std::vector<std::string> tokenizeLine(std::string_view line);

    // Process a single command line.
    bool processCommand(std::string_view rawLine);

    // Register all built-in commands.
    void registerCommands();

    // Print help text.
    void printHelp();

    // Completion callback for the REPL.
    std::vector<std::string> onCompletion(std::string_view prefix);

    // Current state
    std::string m_configPath;
    std::string m_currentGroup;
    std::string m_currentPwd = "/apps";
    std::string m_promptPrefix;
    bool m_running = false;
    bool m_useLocalRpc = false;

    // Core components
    CommandDispatcher m_dispatcher;
    RpcConnection::Ptr m_connection;
    ConsoleConfig m_consoleConfig;
    KeyManager::Ptr m_keyManager;
    ConsoleRepl m_repl;
};

}  // namespace bcos::console
