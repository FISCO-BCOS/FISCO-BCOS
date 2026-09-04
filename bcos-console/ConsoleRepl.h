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
 * @brief: REPL wrapper using replxx — line editing, history, and tab completion
 * @file: ConsoleRepl.h
 */

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace replxx
{
class Replxx;
}

namespace bcos::console
{

// Callback type: given a prefix, returns completion candidates.
using CompletionCallback = std::function<std::vector<std::string>(std::string_view)>;

// REPL backed by the replxx library (BSD-licensed, cross-platform).
// Provides line editing, persistent history, tab completion, hints, and
// Ctrl‑R history search out of the box.
class ConsoleRepl
{
public:
    ConsoleRepl();
    ~ConsoleRepl();

    // Set the prompt string shown before each input line.
    void setPrompt(std::string prompt) { m_prompt = std::move(prompt); }

    // Set the tab-completion callback.
    void setCompleter(CompletionCallback completer) { m_completer = std::move(completer); }

    // Set the history file path for persistence.
    void setHistoryFile(std::string_view path) { m_historyFile = path; }

    // Read one line from the user. Returns the input, or an empty string on EOF.
    std::string readLine();

    // Load history from the configured history file.
    void loadHistory();

    // Save history to the configured history file.
    void saveHistory();

    // Add a line to the in-memory history (deduplicates consecutive duplicates).
    void addHistory(std::string_view line);

private:
    std::unique_ptr<replxx::Replxx> m_rx;
    std::string m_prompt;
    CompletionCallback m_completer;
    std::string m_historyFile;
};

}  // namespace bcos::console
