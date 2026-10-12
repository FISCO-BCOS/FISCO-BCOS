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
 * @brief every page of the panel as a pure render function over (Model, Nav, frame size), so
 *        the interactive loop, --snapshot-dir and the render tests draw the same thing.
 *        Visual rules (spec §5.2.7): 16 ANSI colours, labels dim, ok green / warn yellow /
 *        FAIL red, focused row inverted, title bar inverted, current page tab bold, light
 *        borders, status words ok, FAIL, skip and "*".
 * @file Pages.h
 */
#pragma once

#include "bcos-ops/tui/Model.h"
#include "bcos-ops/tui/Nav.h"
#include <cstdint>
#include <ftxui/dom/elements.hpp>
#include <string>

namespace bcos::ops::tui
{
struct Frame
{
    int width = 80;
    int height = 24;
    int64_t nowMs = 0;           // for "2s ago"; the caller passes the wall clock
    ftxui::Element filterInput;  // the log page's filter box (an Input when interactive)
};

ftxui::Element renderTopBar(Model const& _model);
/// records the label spans in _nav.barHits (mouse) and marks the current page bold
ftxui::Element renderBottomBar(Model const& _model, Nav& _nav, int _width);
/// status band + blocks + txs; side by side at >= 100 columns, stacked below. Updates the
/// list counts / page rows in _nav so the key map moves within what is shown
ftxui::Element renderHome(Model const& _model, Nav& _nav, Frame const& _frame);
ftxui::Element renderConsensus(Model const& _model);
/// identity, sync/peers table, p2p count and the checks with their detail
ftxui::Element renderNode(Model const& _model);
ftxui::Element renderLog(Model const& _model, std::string const& _filter);
ftxui::Element renderLogPage(
    Model const& _model, Nav const& _nav, Frame const& _frame, std::string const& _filter);
ftxui::Element renderSend(Model const& _model);
ftxui::Element renderHelp(Model const& _model);
/// the top of the detail stack (block / tx / proposal); the row index in DetailRef is resolved
/// against the explorer rows. Ticket 14 fills the bodies; this draws the header and keeps
/// _nav.detailTxs sized to the block's transactions
ftxui::Element renderDetail(Model const& _model, Nav& _nav, Frame const& _frame);
/// top bar + page (or detail) + bottom bar, with the help layer over it when open
ftxui::Element renderFrame(
    Model const& _model, Nav& _nav, Frame const& _frame, std::string const& _filter);

// helpers shared by the pages (also used by the tests)
std::string ageText(int64_t _ms);
std::string clockText(int64_t _timestampMs);
std::string shortNodeId(std::string const& _hex);
std::string txStatusWord(int _status);
}  // namespace bcos::ops::tui
