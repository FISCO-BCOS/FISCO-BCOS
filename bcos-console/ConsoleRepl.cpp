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
 * @brief: REPL implementation via replxx library
 * @file: ConsoleRepl.cpp
 */

#include "ConsoleRepl.h"
#include <replxx.hxx>
#include <algorithm>
#include <iostream>
#include <unistd.h>

using namespace bcos::console;

// ---- helpers ----

namespace
{

// Extract the word under the cursor from the full input line.
// Returns (startPos, word).
std::pair<size_t, std::string_view> wordAtCursor(std::string const& input, int cursorPos)
{
    auto pos = static_cast<size_t>(std::max(cursorPos, 0));
    if (pos > input.size())
        pos = input.size();

    auto start = input.rfind(' ', pos - 1);
    if (start == std::string::npos)
        start = 0;
    else
        start = start + 1;

    return {start, std::string_view(input).substr(start, pos - start)};
}

}  // anonymous namespace

// ---- ConsoleRepl ----

ConsoleRepl::ConsoleRepl() : m_rx(std::make_unique<replxx::Replxx>())
{
    m_rx->install_window_change_handler();

    // Treat space and tab as word boundaries for completion
    m_rx->set_word_break_characters(" \t");

    // Tab completion: forward to the user-supplied CompletionCallback.
    // replxx expects completions_t = std::vector<Completion>;
    // adapt from std::vector<std::string>.
    m_rx->set_completion_callback([this](std::string const& input, int& contextLen) {
        if (!m_completer)
            return replxx::Replxx::completions_t{};

        auto [start, word] = wordAtCursor(input, contextLen);
        contextLen = static_cast<int>(start + word.size());
        auto candidates = m_completer(word);

        replxx::Replxx::completions_t result;
        result.reserve(candidates.size());
        for (auto& c : candidates)
            result.emplace_back(std::move(c));
        return result;
    });

    // Hint: show a greyed-out unique completion suffix (fish-shell style).
    // replxx hint_callback_t returns hints_t = std::vector<std::string>.
    m_rx->set_hint_callback(
        [this](std::string const& input, int& contextLen,
               replxx::Replxx::Color& /*color*/) -> replxx::Replxx::hints_t {
            if (!m_completer)
                return {};

            auto [start, word] = wordAtCursor(input, contextLen);
            contextLen = static_cast<int>(start + word.size());
            auto cands = m_completer(word);
            if (cands.size() == 1)
                return {cands[0].substr(word.size())};
            return {};
        });
}

ConsoleRepl::~ConsoleRepl() = default;

std::string ConsoleRepl::readLine()
{
    // Non-interactive (piped) input — delegate to std::getline
    if (!isatty(STDIN_FILENO))
    {
        std::string line;
        if (!std::getline(std::cin, line))
            return {};
        // Trim trailing whitespace to match previous behaviour
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        return line;
    }

    char const* raw = m_rx->input(m_prompt);
    if (raw == nullptr)
        return {};  // Ctrl‑D / EOF

    std::string line(raw);
    // Trim trailing whitespace to match previous behaviour
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
        line.pop_back();

    return line;
}

void ConsoleRepl::loadHistory()
{
    if (!m_historyFile.empty())
        m_rx->history_load(m_historyFile);
}

void ConsoleRepl::saveHistory()
{
    if (!m_historyFile.empty())
        m_rx->history_save(m_historyFile);
}

void ConsoleRepl::addHistory(std::string_view line)
{
    if (!line.empty())
        m_rx->history_add(std::string(line));
}
