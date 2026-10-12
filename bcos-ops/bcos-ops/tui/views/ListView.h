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
 * @brief one scrolling list with fixed-width columns: the home page's blocks / txs lists and
 *        the transaction list inside a block detail draw through it. Columns carry a priority
 *        so the narrower terminal drops the least important ones first.
 * @file ListView.h
 */
#pragma once

#include "bcos-ops/tui/Nav.h"
#include <algorithm>
#include <deque>
#include <ftxui/dom/elements.hpp>
#include <functional>
#include <string>
#include <vector>

namespace bcos::ops::tui
{
using ftxui::Element;
using ftxui::Elements;

struct ListColumn
{
    std::string name;
    int width;
    int priority;  // lower = kept longer
};

/// the columns that fit in _width, highest priority first dropped last; order preserved
inline std::vector<ListColumn> fitListColumns(std::vector<ListColumn> _all, int _width)
{
    auto total = [&]() {
        int sum = 0;
        for (auto const& column : _all)
        {
            sum += column.width + 1;
        }
        return sum - 1;
    };
    while (_all.size() > 1 && total() > _width)
    {
        auto drop = std::max_element(_all.begin(), _all.end(),
            [](ListColumn const& a, ListColumn const& b) { return a.priority < b.priority; });
        _all.erase(drop);
    }
    return _all;
}

inline Element listCell(std::string const& _text, int _width)
{
    return text(_text) | size(WIDTH, EQUAL, _width);
}

inline Element listRow(std::vector<std::pair<ListColumn const*, std::string>> const& _cells)
{
    Elements cells;
    for (size_t i = 0; i < _cells.size(); ++i)
    {
        if (i > 0)
        {
            cells.push_back(text(" "));
        }
        cells.push_back(listCell(_cells[i].second, _cells[i].first->width));
    }
    return hbox(std::move(cells));
}

template <typename Row>
Element renderListView(std::string const& _title, std::vector<ListColumn> const& _columns,
    std::deque<Row> const& _rows, ListState& _list, bool _focused, int _height,
    std::function<std::string(Row const&, ListColumn const&)> const& _cell,
    std::function<bool(Row const&)> const& _gapAfter)
{
    _list.count = _rows.size();
    // border 2 + header; the one gap marker line (older rows skipped) takes a line of its own
    // without counting as a row, so the selected last row never falls off the page
    bool anyGap = std::any_of(_rows.begin(), _rows.end(), _gapAfter);
    _list.pageRows = static_cast<size_t>(std::max(1, _height - 3 - (anyGap ? 1 : 0)));
    if (_list.count > 0 && _list.selected >= _list.count)
    {
        _list.selected = _list.count - 1;
    }
    Elements lines;
    std::vector<std::pair<ListColumn const*, std::string>> header;
    for (auto const& column : _columns)
    {
        header.emplace_back(&column, column.name);
    }
    lines.push_back(listRow(header) | dim);
    auto first = _list.firstVisible();
    size_t drawn = 0;
    for (size_t i = first; i < _rows.size() && drawn < _list.pageRows; ++i, ++drawn)
    {
        std::vector<std::pair<ListColumn const*, std::string>> cells;
        for (auto const& column : _columns)
        {
            cells.emplace_back(&column, _cell(_rows[i], column));
        }
        auto line = listRow(cells);
        if (i == _list.selected)
        {
            line = _focused ? line | inverted : line | bold;
        }
        lines.push_back(line);
        if (_gapAfter(_rows[i]))
        {
            lines.push_back(text(" ~ older rows skipped ~") | dim);
        }
    }
    if (_rows.empty())
    {
        lines.push_back(text("waiting for the first refresh") | dim);
    }
    auto title = text(_title);
    if (_focused)
    {
        title = title | bold;
    }
    return window(title, vbox(std::move(lines)), LIGHT);
}

}  // namespace bcos::ops::tui
