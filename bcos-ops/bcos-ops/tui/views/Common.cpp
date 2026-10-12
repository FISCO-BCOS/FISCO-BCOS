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
 * @file Common.cpp
 */
#include "Pages.h"
#include "bcos-ops/Output.h"
#include <chrono>
#include <ctime>

namespace bcos::ops::tui
{
using namespace ftxui;

std::string ageText(int64_t _ms)
{
    if (_ms < 0)
    {
        return "-";
    }
    auto seconds = _ms / 1000;
    if (seconds < 60)
    {
        return std::to_string(seconds) + "s";
    }
    if (seconds < 3600)
    {
        return std::to_string(seconds / 60) + "m";
    }
    if (seconds < 86400)
    {
        return std::to_string(seconds / 3600) + "h";
    }
    return std::to_string(seconds / 86400) + "d";
}

std::string clockText(int64_t _timestampMs)
{
    if (_timestampMs <= 0)
    {
        return "--:--";
    }
    auto seconds = static_cast<std::time_t>(_timestampMs / 1000);
    std::tm local{};
    localtime_r(&seconds, &local);
    char buffer[8];
    std::strftime(buffer, sizeof(buffer), "%H:%M", &local);
    return buffer;
}

std::string shortNodeId(std::string const& _hex)
{
    auto body = _hex.starts_with("0x") ? _hex.substr(2) : _hex;
    return body.size() > 8 ? body.substr(0, 8) : body;
}

std::string txStatusWord(int _status)
{
    if (_status < 0)
    {
        return "*";  // receipt not fetched yet
    }
    return _status == 0 ? "ok" : "FAIL";
}

Element renderTopBar(Model const& _model)
{
    auto node = _model.status.nodeId ? shortNodeId(*_model.status.nodeId) : "--------";
    auto source = _model.source.empty() ? "connecting" : _model.source;
    auto mode = _model.mode.empty() || _model.mode == "none" ? "pbft" : _model.mode;
    auto key = _model.keyAddress.empty() ? "none" : abridged(_model.keyAddress, 4);
    auto left = hbox({text(" fisco-bcos tui ") | bold, text(" node " + node),
        text("  source " + source), text("  mode " + mode),
        text("  refresh " + std::to_string(_model.refreshInSeconds) + "s ")});
    if (!_model.error.empty())
    {
        // the failure replaces the status words: it is what the operator must read first
        return hbox({text(" fisco-bcos tui ") | bold,
                   text(" " + sanitizeForTerminal(_model.error) + " "), filler()}) |
               bgcolor(Color::Red) | color(Color::White);
    }
    return hbox({left, filler(), text(" key " + key + " ")}) | inverted;
}

Element renderBottomBar(Model const& _model, Nav& _nav, int _width)
{
    _nav.barHits.clear();
    Elements cells;
    int x = 0;
    auto label = [&](Page page) {
        std::string word =
            " " + std::to_string(static_cast<int>(page)) + " " + pageName(page) + " ";
        _nav.barHits.push_back({page, x, x + static_cast<int>(word.size())});
        x += static_cast<int>(word.size());
        auto cell = text(word);
        if (!_nav.inDetail() && page == _nav.page)
        {
            cell = cell | bold;
        }
        cells.push_back(cell);
    };
    for (auto page : {Page::home, Page::consensus, Page::send, Page::log, Page::node})
    {
        label(page);
    }
    if (_model.authChain)
    {
        label(Page::gov);
    }
    std::string keys;
    if (_width >= 100)
    {
        bool proposal = _nav.inDetail() && _nav.stack.back().kind == DetailKind::proposal;
        keys = proposal        ? "  Esc back  v for  x against  d revoke  ? help  q quit" :
               _nav.inDetail() ? "  Esc back  j k select  Enter open  ? help  q quit" :
                                 "  Tab focus  j k select  Enter open  ? help  q quit";
    }
    else
    {
        keys = _nav.inDetail() ? "  Esc back  ? help  q quit" : "  ? help  q quit";
    }
    cells.push_back(text(keys) | dim);
    return hbox(std::move(cells));
}

Element renderHelp(Model const& _model)
{
    Elements lines = {
        text("1-5   pages: home consensus send log node"),
        text(_model.authChain ? "6     gov (auth chain)" : "6     gov (auth chains only)") | dim,
        text("Tab   next focus area (home: blocks / txs; log: list / filter)"),
        text("j k   or arrows: select a row;  PgUp PgDn: a page of rows"),
        text("Enter open the selected block / tx / proposal;  Esc: back"),
        text("send  Tab: tree / key file / fields / button;  Enter on [ send ] confirms"),
        text("gov   Enter opens a proposal;  v vote for, x vote against, d revoke (confirmed)"),
        text("r     refresh now;  q: quit;  ?: this help"),
        text(""),
        text("any key closes this layer") | dim,
    };
    return window(text(" help "), vbox(std::move(lines)), LIGHT);
}

Element renderLogPage(
    Model const& _model, Nav const& _nav, Frame const& _frame, std::string const& _filter)
{
    auto filterLabel = text(" filter> ");
    if (!_nav.inDetail() && _nav.page == Page::log && _nav.focus == 1)
    {
        filterLabel = filterLabel | bold;
    }
    auto input = _frame.filterInput ? _frame.filterInput : text(_filter);
    return window(text(" log "),
        vbox({hbox({filterLabel, input | flex}), separatorLight(), renderLog(_model, _filter)}),
        LIGHT);
}

Element renderFrame(Model const& _model, Nav& _nav, Frame const& _frame, std::string const& _filter,
    SendForm* _form)
{
    _nav.authChain = _model.authChain;
    Frame body = _frame;
    body.height = std::max(1, _frame.height - 2);  // top and bottom bars
    Element page;
    if (_nav.inDetail())
    {
        resolveDetailRefs(_nav, _model.explorer);
        resolveProposalRefs(_nav, _model.governance);
        page = renderDetail(_model, _nav, body, _form);
    }
    else
    {
        switch (_nav.page)
        {
        case Page::home:
            page = renderHome(_model, _nav, body);
            break;
        case Page::consensus:
            page = renderConsensus(_model);
            break;
        case Page::send:
        {
            static SendForm s_snapshotForm = makeSendForm(false);  // headless renders only
            page = renderSend(_model, _form ? *_form : s_snapshotForm, body);
            break;
        }
        case Page::log:
            page = renderLogPage(_model, _nav, body, _filter);
            break;
        case Page::node:
            page = renderNode(_model);
            break;
        case Page::gov:
            page = renderGov(_model, _nav, body);
            break;
        }
    }
    _nav.barRow = _frame.height - 1;
    auto frame =
        vbox({renderTopBar(_model), page | flex, renderBottomBar(_model, _nav, _frame.width)});
    bool onProposal = _nav.inDetail() && _nav.stack.back().kind == DetailKind::proposal;
    if (_form && _form->confirm && ((!_nav.inDetail() && _nav.page == Page::send) || onProposal))
    {
        frame = dbox({frame, renderSendConfirm(*_form) | clear_under | center});
    }
    if (_nav.help)
    {
        frame = dbox({frame, renderHelp(_model) | clear_under | center});
    }
    return frame;
}
}  // namespace bcos::ops::tui
