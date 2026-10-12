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
 * @brief navigation state of the panel: current page, focus area, list selections, the detail
 *        stack and the help overlay, plus the key/mouse dispatch that moves between them. No
 *        rendering and no model access, so it is tested with bare events.
 * @file Nav.h
 */
#pragma once

#include <array>
#include <cstdint>
#include <ftxui/component/event.hpp>
#include <string>
#include <vector>

namespace bcos::ops::tui
{
enum class Page
{
    home = 1,
    consensus = 2,
    send = 3,
    log = 4,
    node = 5,
    gov = 6
};

enum class DetailKind
{
    block,
    tx,
    proposal
};

struct DetailRef
{
    DetailKind kind = DetailKind::block;
    // pushed by Nav as the selected row index; resolveDetailRefs (views/Detail.cpp) turns it into
    // the block height (block) or the block height + hash (tx) so new rows arriving above do not
    // move the page to another object
    int64_t number = -1;  // block height or proposal id; row index while !resolved
    std::string hash;     // tx hash (resolved) / block hash
    bool resolved = false;
};

/// a list the cursor can move in: the renderer reports how many rows exist and fit
struct ListState
{
    size_t selected = 0;
    size_t count = 0;     // rows available (renderer updates before dispatch)
    size_t pageRows = 1;  // rows that fit on screen (renderer updates)
    size_t firstVisible() const { return selected >= pageRows ? selected - pageRows + 1 : 0; }
};

/// what the bottom bar laid out, so a click can be mapped back to a page
struct BarHit
{
    Page page;
    int xFrom;
    int xTo;  // exclusive
};

enum class NavAction
{
    none,
    redraw,
    refresh,
    quit
};

struct Nav
{
    Page page = Page::home;
    int focus =
        0;  // focus area index within the page (home: 0 blocks, 1 txs; log: 0 list, 1 filter)
    ListState blocks;
    ListState txs;
    ListState proposals;
    ListState detailTxs;  // the tx list inside a block detail
    std::vector<DetailRef> stack;
    bool help = false;
    bool authChain = false;  // page 6 exists
    std::vector<BarHit> barHits;
    int barRow = -1;  // screen row of the bottom bar (mouse)

    bool inDetail() const { return !stack.empty(); }
    int focusAreas() const;
    /// the list the cursor currently moves in, or nullptr
    ListState* focusedList();
};

/// page from a '1'..'6' key; gov only on auth chains
bool pageFromKey(Nav const& _nav, char _key, Page& _page);
NavAction navHandle(Nav& _nav, ftxui::Event _event);  // by value: Event::mouse() is non-const
std::string pageName(Page _page);
}  // namespace bcos::ops::tui
