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
#include "bcos-ops/tui/SendForm.h"
#include <cstdint>
#include <ftxui/dom/elements.hpp>
#include <map>
#include <string>
#include <vector>

namespace bcos::ops::tui
{
struct Frame
{
    int width = 80;
    int height = 24;
    int64_t nowMs = 0;           // for "2s ago"; the caller passes the wall clock
    ftxui::Element filterInput;  // the log page's filter box (an Input when interactive)
    // the send page's Input components by field id ("key", "to", "sig", "value", "raw",
    // "p0".."p7"); a missing id renders the plain value (snapshots, tests)
    std::map<std::string, ftxui::Element> inputs;
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
/// preset tree on the left, the selected entry's form on the right, the last outcome below;
/// updates _form.tree / featureList page rows and counts
ftxui::Element renderSend(Model const& _model, SendForm& _form, Frame const& _frame);
/// the confirmation layer: mode / to / method / params and the Enter / Esc hint
ftxui::Element renderSendConfirm(SendForm const& _form);
ftxui::Element renderHelp(Model const& _model);
/// committee rates + governors above, the newest proposals below (auth chains; otherwise a
/// one-line note); updates _nav.proposals
ftxui::Element renderGov(Model const& _model, Nav& _nav, Frame const& _frame);
/// one proposal full screen with the v / x / d hints; _form (when given) supplies the notice
/// line of the key map and the last outcome comes from _model.send
ftxui::Element renderProposalDetail(
    Model const& _model, Frame const& _frame, DetailRef const& _ref, SendForm const* _form);
/// maps a proposal ref pushed as a row index to its proposal id (resolved); keeps resolved ones
void resolveProposalRefs(Nav& _nav, GovernanceState const& _governance);
/// the list size and open proposal ids the refresher fetches for the gov page
GovernanceWants governanceWants(Nav const& _nav);
/// maps the row indexes Nav pushed into stable keys (block height, tx hash) using the explorer
/// rows and the cached block details; a tx opened from a block whose detail is not loaded yet
/// stays unresolved until the next render
void resolveDetailRefs(Nav& _nav, ExplorerState const& _explorer);
/// the block / tx details the open stack needs from the refresher
std::vector<DetailWant> detailWants(Nav const& _nav);
/// the top of the detail stack (block / tx / proposal) full screen; keeps _nav.detailTxs sized
/// to the block's transactions
ftxui::Element renderDetail(
    Model const& _model, Nav& _nav, Frame const& _frame, SendForm const* _form = nullptr);
/// `Error(string)` revert reason of a receipt output, or empty
std::string detailRevertReason(std::string const& _outputHex);
/// top bar + page (or detail) + bottom bar, with the help layer over it when open
ftxui::Element renderFrame(Model const& _model, Nav& _nav, Frame const& _frame,
    std::string const& _filter, SendForm* _form = nullptr);

// helpers shared by the pages (also used by the tests)
std::string ageText(int64_t _ms);
std::string clockText(int64_t _timestampMs);
std::string shortNodeId(std::string const& _hex);
std::string txStatusWord(int _status);
}  // namespace bcos::ops::tui
