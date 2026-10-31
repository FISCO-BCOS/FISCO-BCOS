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
 * @brief headless rendering for the tui tests: an element drawn on a fixed screen, ANSI
 *        style sequences stripped so assertions see the text the terminal shows
 * @file Render.h
 */
#pragma once

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <string>

namespace bcos::ops::test
{
inline std::string stripAnsi(std::string const& _rendered)
{
    std::string out;
    for (size_t i = 0; i < _rendered.size(); ++i)
    {
        if (_rendered[i] == '\x1b')
        {
            while (i < _rendered.size() && _rendered[i] != 'm')
            {
                ++i;
            }
            continue;
        }
        out += _rendered[i];
    }
    return out;
}

inline std::string renderRaw(ftxui::Element _element, int _width = 80, int _height = 24)
{
    auto screen =
        ftxui::Screen::Create(ftxui::Dimension::Fixed(_width), ftxui::Dimension::Fixed(_height));
    ftxui::Render(screen, _element);
    return screen.ToString();
}

inline std::string renderText(ftxui::Element _element, int _width = 80, int _height = 24)
{
    return stripAnsi(renderRaw(std::move(_element), _width, _height));
}

/// the screen line containing _needle, or empty
inline std::string lineWith(std::string const& _text, std::string const& _needle)
{
    auto pos = _text.find(_needle);
    if (pos == std::string::npos)
    {
        return "";
    }
    auto start = _text.rfind('\n', pos);
    start = start == std::string::npos ? 0 : start + 1;
    auto end = _text.find('\n', pos);
    return _text.substr(start, end - start);
}
}  // namespace bcos::ops::test
