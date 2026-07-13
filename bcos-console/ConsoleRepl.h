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
 * @brief: built-in REPL with line editing, history, and tab completion
 * @file: ConsoleRepl.h
 */

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <termios.h>

namespace bcos::console
{

// Callback type: given a prefix, returns completion candidates.
using CompletionCallback = std::function<std::vector<std::string>(std::string_view)>;

// Built-in REPL with terminal raw-mode line editing and history.
// No external dependencies — uses ANSI escape sequences and termios.
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

    // Read one line from the user. Returns the raw input (may be empty on Ctrl-D).
    // Throws on terminal errors.
    std::string readLine();

    // Load history from the configured history file.
    void loadHistory();

    // Save history to the configured history file.
    void saveHistory();

    // Add a line to the in-memory history.
    void addHistory(std::string_view line);

private:
    void enableRawMode();
    void disableRawMode();
    void refreshLine();
    void handleCompletion();

    std::string m_prompt;
    CompletionCallback m_completer;

    std::string m_buffer;          // current input line
    size_t m_cursor = 0;          // cursor position in m_buffer
    std::vector<std::string> m_history;
    int m_historyIndex = -1;      // -1 = not navigating history
    std::string m_savedLine;      // saved line before history navigation

    std::string m_historyFile;

    // Terminal dimensions
    int m_termCols = 80;

    // Original terminal settings (system ::termios, not namespaced)
    ::termios* m_origTermios = nullptr;
    bool m_rawMode = false;
};

}  // namespace bcos::console
