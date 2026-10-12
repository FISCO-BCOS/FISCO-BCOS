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
 * @file Nav.cpp
 */
#include "Nav.h"
#include <algorithm>

namespace bcos::ops::tui
{
namespace
{
void navMove(ListState& _list, int64_t _delta)
{
    if (_list.count == 0)
    {
        _list.selected = 0;
        return;
    }
    auto target = static_cast<int64_t>(_list.selected) + _delta;
    target = std::clamp<int64_t>(target, 0, static_cast<int64_t>(_list.count) - 1);
    _list.selected = static_cast<size_t>(target);
}

NavAction navOpen(Nav& _nav)
{
    if (_nav.inDetail())
    {
        // a block detail lists its transactions: Enter there opens the transaction
        if (_nav.stack.back().kind == DetailKind::block && _nav.detailTxs.count > 0)
        {
            DetailRef ref;
            ref.kind = DetailKind::tx;
            ref.number = static_cast<int64_t>(_nav.detailTxs.selected);  // index in the block
            ref.hash = "";  // the renderer resolves index → hash (ticket 14)
            _nav.stack.push_back(std::move(ref));
            return NavAction::redraw;
        }
        return NavAction::none;
    }
    if (_nav.page == Page::home)
    {
        DetailRef ref;
        if (_nav.focus == 0)
        {
            if (_nav.blocks.count == 0)
            {
                return NavAction::none;
            }
            ref.kind = DetailKind::block;
            ref.number =
                static_cast<int64_t>(_nav.blocks.selected);  // row index; resolved by the renderer
        }
        else
        {
            if (_nav.txs.count == 0)
            {
                return NavAction::none;
            }
            ref.kind = DetailKind::tx;
            ref.number = static_cast<int64_t>(_nav.txs.selected);
        }
        _nav.stack.push_back(std::move(ref));
        return NavAction::redraw;
    }
    if (_nav.page == Page::gov && _nav.proposals.count > 0)
    {
        DetailRef ref;
        ref.kind = DetailKind::proposal;
        ref.number = static_cast<int64_t>(_nav.proposals.selected);
        _nav.stack.push_back(std::move(ref));
        return NavAction::redraw;
    }
    return NavAction::none;
}
}  // namespace

int Nav::focusAreas() const
{
    if (inDetail())
    {
        return 1;
    }
    switch (page)
    {
    case Page::home:
    case Page::log:
        return 2;
    default:
        return 1;
    }
}

ListState* Nav::focusedList()
{
    if (inDetail())
    {
        return stack.back().kind == DetailKind::block ? &detailTxs : nullptr;
    }
    switch (page)
    {
    case Page::home:
        return focus == 0 ? &blocks : &txs;
    case Page::gov:
        return &proposals;
    default:
        return nullptr;
    }
}

bool pageFromKey(Nav const& _nav, char _key, Page& _page)
{
    if (_key < '1' || _key > '6')
    {
        return false;
    }
    if (_key == '6' && !_nav.authChain)
    {
        return false;
    }
    _page = static_cast<Page>(_key - '0');
    return true;
}

std::string pageName(Page _page)
{
    switch (_page)
    {
    case Page::home:
        return "home";
    case Page::consensus:
        return "consensus";
    case Page::send:
        return "send";
    case Page::log:
        return "log";
    case Page::node:
        return "node";
    case Page::gov:
        return "gov";
    }
    return "?";
}

NavAction navHandle(Nav& _nav, ftxui::Event _event)
{
    using ftxui::Event;
    if (_event.is_mouse())
    {
        auto const& mouse = _event.mouse();
        if (mouse.button != ftxui::Mouse::Left || mouse.motion != ftxui::Mouse::Pressed ||
            mouse.y != _nav.barRow)
        {
            return NavAction::none;
        }
        for (auto const& hit : _nav.barHits)
        {
            if (mouse.x >= hit.xFrom && mouse.x < hit.xTo)
            {
                _nav.page = hit.page;
                _nav.stack.clear();
                _nav.focus = 0;
                _nav.help = false;
                return NavAction::redraw;
            }
        }
        return NavAction::none;
    }
    if (_nav.help)
    {
        // any key closes the help layer
        _nav.help = false;
        return NavAction::redraw;
    }
    if (_event == Event::Character('q'))
    {
        return NavAction::quit;
    }
    if (_event == Event::Character('?'))
    {
        _nav.help = true;
        return NavAction::redraw;
    }
    if (_event == Event::Character('r'))
    {
        return NavAction::refresh;
    }
    if (_event == Event::Escape)
    {
        if (_nav.inDetail())
        {
            _nav.stack.pop_back();
            return NavAction::redraw;
        }
        return NavAction::none;
    }
    if (_event == Event::Return)
    {
        return navOpen(_nav);
    }
    if (_event == Event::Tab || _event == Event::TabReverse)
    {
        auto areas = _nav.focusAreas();
        if (areas <= 1)
        {
            return NavAction::none;
        }
        _nav.focus =
            _event == Event::Tab ? (_nav.focus + 1) % areas : (_nav.focus + areas - 1) % areas;
        return NavAction::redraw;
    }
    if (_event.is_character() && _event.character().size() == 1)
    {
        char key = _event.character()[0];
        Page page;
        if (!_nav.inDetail() && pageFromKey(_nav, key, page))
        {
            _nav.page = page;
            _nav.focus = 0;
            return NavAction::redraw;
        }
        if (key == 'j' || key == 'k')
        {
            if (auto* list = _nav.focusedList())
            {
                navMove(*list, key == 'j' ? 1 : -1);
                return NavAction::redraw;
            }
        }
        return NavAction::none;
    }
    auto* list = _nav.focusedList();
    if (list == nullptr)
    {
        return NavAction::none;
    }
    if (_event == Event::ArrowDown)
    {
        navMove(*list, 1);
        return NavAction::redraw;
    }
    if (_event == Event::ArrowUp)
    {
        navMove(*list, -1);
        return NavAction::redraw;
    }
    if (_event == Event::PageDown)
    {
        navMove(*list, static_cast<int64_t>(std::max<size_t>(1, list->pageRows)));
        return NavAction::redraw;
    }
    if (_event == Event::PageUp)
    {
        navMove(*list, -static_cast<int64_t>(std::max<size_t>(1, list->pageRows)));
        return NavAction::redraw;
    }
    return NavAction::none;
}
}  // namespace bcos::ops::tui
