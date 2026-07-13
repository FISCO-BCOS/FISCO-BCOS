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
 * @brief: built-in REPL implementation with terminal raw mode
 * @file: ConsoleRepl.cpp
 */

#include "ConsoleRepl.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

using namespace bcos::console;

// ---- Terminal helpers ----

static void writeStr(std::string_view s)
{
    [[maybe_unused]] auto _ = write(STDOUT_FILENO, s.data(), s.size());
}

static constexpr auto CSI = "\x1b[";  // Control Sequence Introducer

// ANSI cursor/line commands
#define ANSI_CURSOR_UP(n)    "\x1b[" #n "A"
#define ANSI_CURSOR_DOWN(n)  "\x1b[" #n "B"
#define ANSI_CURSOR_FWD(n)   "\x1b[" #n "C"
#define ANSI_CURSOR_BACK(n)  "\x1b[" #n "D"
#define ANSI_CLEAR_LINE      "\x1b[2K"
#define ANSI_CLEAR_EOL       "\x1b[0K"
#define ANSI_SAVE_CURSOR     "\x1b[s"
#define ANSI_RESTORE_CURSOR  "\x1b[u"

ConsoleRepl::ConsoleRepl()
{
    m_origTermios = new ::termios;
    tcgetattr(STDIN_FILENO, m_origTermios);

    // Get terminal width
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        m_termCols = ws.ws_col;
}

ConsoleRepl::~ConsoleRepl()
{
    disableRawMode();
    delete m_origTermios;
}

void ConsoleRepl::enableRawMode()
{
    if (m_rawMode) return;
    ::termios raw = *m_origTermios;
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL);
    raw.c_oflag &= ~(OPOST);
    raw.c_cc[VMIN] = 1;   // block until at least 1 byte available
    raw.c_cc[VTIME] = 0;  // no timeout
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    m_rawMode = true;
}

void ConsoleRepl::disableRawMode()
{
    if (!m_rawMode) return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, m_origTermios);
    m_rawMode = false;
}

void ConsoleRepl::refreshLine()
{
    // Move to line start, clear line, redraw prompt + buffer
    writeStr("\r");
    writeStr(ANSI_CLEAR_LINE);
    writeStr(m_prompt);
    writeStr(m_buffer);

    // Position cursor correctly
    if (m_cursor < m_buffer.size())
    {
        writeStr("\r" ANSI_CURSOR_FWD(static_cast<int>(m_prompt.size() + m_cursor)));
    }
}

// ---- ESC sequence readers ----

static bool readSeq(std::string_view seq, int fd)
{
    char c;
    for (size_t i = 0; i < seq.size(); ++i)
    {
        if (read(fd, &c, 1) != 1 || c != seq[i])
            return false;
    }
    return true;
}

std::string ConsoleRepl::readLine()
{
    // If stdin is not a TTY (e.g. piped input), fall back to std::getline
    if (!isatty(STDIN_FILENO))
    {
        disableRawMode();
        std::string line;
        if (!std::getline(std::cin, line))
            return {};  // EOF
        // Trim trailing whitespace
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        if (!line.empty() && (m_history.empty() || m_history.back() != line))
            m_history.push_back(line);
        return line;
    }

    enableRawMode();
    m_buffer.clear();
    m_cursor = 0;
    m_historyIndex = -1;

    writeStr(m_prompt);

    while (true)
    {
        char c = 0;
        if (read(STDIN_FILENO, &c, 1) != 1)
        {
            if (errno == EAGAIN) continue;
            break;
        }

        if (c == '\x1b')  // ESC — possible arrow key or other sequence
        {
            char c2 = 0;
            if (read(STDIN_FILENO, &c2, 1) != 1) continue;

            if (c2 == '[')
            {
                char c3 = 0;
                if (read(STDIN_FILENO, &c3, 1) != 1) continue;

                switch (c3)
                {
                case 'A':  // Up arrow — previous history
                    if (m_history.empty()) break;
                    if (m_historyIndex == -1)
                    {
                        m_savedLine = m_buffer;
                        m_historyIndex = static_cast<int>(m_history.size()) - 1;
                    }
                    else if (m_historyIndex > 0)
                    {
                        m_historyIndex--;
                    }
                    m_buffer = m_history[m_historyIndex];
                    m_cursor = m_buffer.size();
                    refreshLine();
                    break;

                case 'B':  // Down arrow — next history
                    if (m_historyIndex == -1) break;
                    m_historyIndex++;
                    if (m_historyIndex >= static_cast<int>(m_history.size()))
                    {
                        m_historyIndex = -1;
                        m_buffer = m_savedLine;
                    }
                    else
                    {
                        m_buffer = m_history[m_historyIndex];
                    }
                    m_cursor = m_buffer.size();
                    refreshLine();
                    break;

                case 'C':  // Right arrow
                    if (m_cursor < m_buffer.size())
                    {
                        m_cursor++;
                        writeStr(ANSI_CURSOR_FWD(1));
                    }
                    break;

                case 'D':  // Left arrow
                    if (m_cursor > 0)
                    {
                        m_cursor--;
                        writeStr(ANSI_CURSOR_BACK(1));
                    }
                    break;

                case 'H':  // Home
                    if (m_cursor > 0)
                    {
                        writeStr("\r" ANSI_CURSOR_FWD(static_cast<int>(m_prompt.size())));
                        m_cursor = 0;
                    }
                    break;

                case 'F':  // End
                    if (m_cursor < m_buffer.size())
                    {
                        writeStr(ANSI_CURSOR_FWD(
                            static_cast<int>(m_buffer.size() - m_cursor)));
                        m_cursor = m_buffer.size();
                    }
                    break;

                case '3':  // Delete key (ESC [ 3 ~)
                    if (readSeq("~", STDIN_FILENO))
                    {
                        if (m_cursor < m_buffer.size())
                        {
                            m_buffer.erase(m_cursor, 1);
                            refreshLine();
                        }
                    }
                    break;
                }
            }
        }
        else if (c == '\t')  // Tab — completion
        {
            handleCompletion();
        }
        else if (c == '\n' || c == '\r')  // Enter
        {
            writeStr("\r\n");
            break;
        }
        else if (c == 0x7f || c == '\b')  // Backspace
        {
            if (m_cursor > 0)
            {
                m_cursor--;
                m_buffer.erase(m_cursor, 1);
                refreshLine();
            }
        }
        else if (c == 0x04)  // Ctrl-D — EOF on empty line, else delete-forward
        {
            if (m_buffer.empty())
            {
                writeStr("\r\n");
                disableRawMode();
                return {};  // signal EOF
            }
            if (m_cursor < m_buffer.size())
            {
                m_buffer.erase(m_cursor, 1);
                refreshLine();
            }
        }
        else if (c == 0x0b)  // Ctrl-K — kill to end of line
        {
            if (m_cursor < m_buffer.size())
            {
                m_buffer.erase(m_cursor);
                refreshLine();
            }
        }
        else if (c == 0x15)  // Ctrl-U — kill to start of line
        {
            if (m_cursor > 0)
            {
                m_buffer.erase(0, m_cursor);
                m_cursor = 0;
                refreshLine();
            }
        }
        else if (c >= 0x20 && c < 0x7f)  // Printable ASCII
        {
            m_buffer.insert(m_cursor, 1, c);
            m_cursor++;
            refreshLine();
        }
    }

    disableRawMode();

    // Trim trailing whitespace
    while (!m_buffer.empty() && (m_buffer.back() == ' ' || m_buffer.back() == '\t'))
        m_buffer.pop_back();

    // Add to history if non-empty and not duplicate of last entry
    if (!m_buffer.empty() &&
        (m_history.empty() || m_history.back() != m_buffer))
    {
        m_history.push_back(m_buffer);
    }

    return m_buffer;
}

void ConsoleRepl::handleCompletion()
{
    if (!m_completer) return;

    // Find the word at cursor position
    auto line = m_buffer;
    if (line.empty()) return;

    // Extract the word from start to cursor
    size_t wordStart = m_cursor;
    while (wordStart > 0 && line[wordStart - 1] != ' ')
        wordStart--;
    auto prefix = line.substr(wordStart, m_cursor - wordStart);

    auto candidates = m_completer(prefix);
    if (candidates.empty()) return;

    if (candidates.size() == 1)
    {
        // Complete the word
        auto suffix = candidates[0].substr(prefix.size());
        m_buffer.insert(m_cursor, suffix);
        m_cursor += suffix.size();
        refreshLine();
    }
    else
    {
        // Show candidates
        writeStr("\r\n");
        for (size_t i = 0; i < candidates.size(); ++i)
        {
            writeStr(candidates[i]);
            writeStr("  ");
        }
        writeStr("\r\n");
        refreshLine();
    }
}

// ---- History persistence ----

void ConsoleRepl::loadHistory()
{
    if (m_historyFile.empty()) return;
    std::ifstream f(m_historyFile);
    if (!f) return;
    std::string line;
    while (std::getline(f, line))
    {
        if (!line.empty())
            m_history.push_back(line);
    }
}

void ConsoleRepl::saveHistory()
{
    if (m_historyFile.empty()) return;
    std::ofstream f(m_historyFile, std::ios::trunc);
    if (!f) return;
    // Save last 1000 entries
    size_t start = m_history.size() > 1000 ? m_history.size() - 1000 : 0;
    for (size_t i = start; i < m_history.size(); ++i)
        f << m_history[i] << '\n';
}

void ConsoleRepl::addHistory(std::string_view line)
{
    if (line.empty()) return;
    if (!m_history.empty() && m_history.back() == line) return;
    m_history.push_back(std::string(line));
}
