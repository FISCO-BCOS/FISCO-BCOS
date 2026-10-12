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
 * @file NavTest.cpp
 */
#include "bcos-ops/tui/Nav.h"
#include <boost/test/unit_test.hpp>

namespace bcos::ops::test
{
using tui::DetailKind;
using tui::Nav;
using tui::NavAction;
using tui::Page;

BOOST_AUTO_TEST_SUITE(NavTest)

BOOST_AUTO_TEST_CASE(digitsSwitchPagesAndGovNeedsAuth)
{
    Nav nav;
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Character('5')) == NavAction::redraw);
    BOOST_CHECK(nav.page == Page::node);
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Character('6')) == NavAction::none);
    BOOST_CHECK(nav.page == Page::node);
    nav.authChain = true;
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Character('6')) == NavAction::redraw);
    BOOST_CHECK(nav.page == Page::gov);
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Character('1')) == NavAction::redraw);
    BOOST_CHECK(nav.page == Page::home);
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Character('7')) == NavAction::none);
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Character('q')) == NavAction::quit);
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Character('r')) == NavAction::refresh);
}

BOOST_AUTO_TEST_CASE(tabCyclesFocusWhereThereAreTwoAreas)
{
    Nav nav;
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Tab) == NavAction::redraw);
    BOOST_CHECK_EQUAL(nav.focus, 1);
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Tab) == NavAction::redraw);
    BOOST_CHECK_EQUAL(nav.focus, 0);
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::TabReverse) == NavAction::redraw);
    BOOST_CHECK_EQUAL(nav.focus, 1);
    tui::navHandle(nav, ftxui::Event::Character('2'));
    BOOST_CHECK_EQUAL(nav.focus, 0);  // a page switch resets the focus
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Tab) == NavAction::none);
}

BOOST_AUTO_TEST_CASE(cursorMovesWithinTheFocusedListBounds)
{
    Nav nav;
    nav.blocks.count = 5;
    nav.blocks.pageRows = 2;
    nav.txs.count = 3;
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::ArrowUp) == NavAction::redraw);
    BOOST_CHECK_EQUAL(nav.blocks.selected, 0U);
    tui::navHandle(nav, ftxui::Event::ArrowDown);
    tui::navHandle(nav, ftxui::Event::Character('j'));
    BOOST_CHECK_EQUAL(nav.blocks.selected, 2U);
    tui::navHandle(nav, ftxui::Event::PageDown);
    BOOST_CHECK_EQUAL(nav.blocks.selected, 4U);
    tui::navHandle(nav, ftxui::Event::PageDown);
    BOOST_CHECK_EQUAL(nav.blocks.selected, 4U);
    BOOST_CHECK_EQUAL(nav.blocks.firstVisible(), 3U);
    tui::navHandle(nav, ftxui::Event::PageUp);
    BOOST_CHECK_EQUAL(nav.blocks.selected, 2U);
    tui::navHandle(nav, ftxui::Event::Character('k'));
    BOOST_CHECK_EQUAL(nav.blocks.selected, 1U);
    tui::navHandle(nav, ftxui::Event::Tab);
    tui::navHandle(nav, ftxui::Event::ArrowDown);
    BOOST_CHECK_EQUAL(nav.txs.selected, 1U);
    BOOST_CHECK_EQUAL(nav.blocks.selected, 1U);  // the other list keeps its cursor
    tui::navHandle(nav, ftxui::Event::Character('2'));
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::ArrowDown) == NavAction::none);  // no list on
                                                                                   // page 2
}

BOOST_AUTO_TEST_CASE(enterOpensDetailsAndEscapePopsTheStack)
{
    Nav nav;
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Return) == NavAction::none);  // empty list
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Escape) == NavAction::none);  // empty stack
    nav.blocks.count = 3;
    nav.txs.count = 2;
    nav.blocks.selected = 2;
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Return) == NavAction::redraw);
    BOOST_REQUIRE_EQUAL(nav.stack.size(), 1U);
    BOOST_CHECK(nav.stack[0].kind == DetailKind::block);
    BOOST_CHECK_EQUAL(nav.stack[0].number, 2);
    BOOST_CHECK(nav.inDetail());
    // digits do not switch pages while a detail is open; the block's tx list is the cursor
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Character('5')) == NavAction::none);
    nav.detailTxs.count = 4;
    tui::navHandle(nav, ftxui::Event::ArrowDown);
    BOOST_CHECK_EQUAL(nav.detailTxs.selected, 1U);
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Return) == NavAction::redraw);
    BOOST_REQUIRE_EQUAL(nav.stack.size(), 2U);
    BOOST_CHECK(nav.stack[1].kind == DetailKind::tx);
    BOOST_CHECK_EQUAL(nav.stack[1].number, 1);
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Return) == NavAction::none);  // a tx has no
                                                                                // children
    tui::navHandle(nav, ftxui::Event::Escape);
    BOOST_CHECK_EQUAL(nav.stack.size(), 1U);
    tui::navHandle(nav, ftxui::Event::Escape);
    BOOST_CHECK(!nav.inDetail());
    BOOST_CHECK_EQUAL(nav.blocks.selected, 2U);  // the home cursor survived the round trip
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Escape) == NavAction::none);
    // from the tx list
    nav.focus = 1;
    nav.txs.selected = 1;
    tui::navHandle(nav, ftxui::Event::Return);
    BOOST_CHECK(nav.stack.back().kind == DetailKind::tx);
    BOOST_CHECK_EQUAL(nav.stack.back().number, 1);
}

BOOST_AUTO_TEST_CASE(helpLayerSwallowsTheNextKey)
{
    Nav nav;
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Character('?')) == NavAction::redraw);
    BOOST_CHECK(nav.help);
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Character('q')) == NavAction::redraw);  // closes,
                                                                                          // no quit
    BOOST_CHECK(!nav.help);
    BOOST_CHECK(nav.page == Page::home);
}

BOOST_AUTO_TEST_CASE(mouseOnTheBottomBarSwitchesPages)
{
    Nav nav;
    nav.barRow = 23;
    nav.barHits = {{Page::home, 1, 7}, {Page::consensus, 9, 20}, {Page::node, 30, 36}};
    ftxui::Mouse mouse;
    mouse.button = ftxui::Mouse::Left;
    mouse.motion = ftxui::Mouse::Pressed;
    mouse.x = 31;
    mouse.y = 23;
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Mouse("", mouse)) == NavAction::redraw);
    BOOST_CHECK(nav.page == Page::node);
    mouse.y = 10;
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Mouse("", mouse)) == NavAction::none);
    mouse.y = 23;
    mouse.x = 25;  // between labels
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Mouse("", mouse)) == NavAction::none);
    mouse.motion = ftxui::Mouse::Released;
    mouse.x = 2;
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Mouse("", mouse)) == NavAction::none);
    BOOST_CHECK(nav.page == Page::node);
    BOOST_CHECK_EQUAL(tui::pageName(nav.page), "node");
}

BOOST_AUTO_TEST_CASE(ctrlCQuitsLikeQ)
{
    Nav nav;
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::CtrlC) == NavAction::quit);
    nav.help = true;
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::CtrlC) == NavAction::quit);  // even over the help
                                                                               // layer
}

BOOST_AUTO_TEST_CASE(homeCursorSurvivesTheDetailStackAndTheTxCursorStartsAtZero)
{
    Nav nav;
    nav.blocks.count = 5;
    nav.blocks.pageRows = 5;
    nav.txs.count = 5;
    tui::navHandle(nav, ftxui::Event::ArrowDown);
    tui::navHandle(nav, ftxui::Event::ArrowDown);
    BOOST_CHECK_EQUAL(nav.blocks.selected, 2U);
    nav.detailTxs.selected = 3;  // left over from an earlier block detail
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Return) == NavAction::redraw);  // block
    BOOST_CHECK_EQUAL(nav.detailTxs.selected, 0U);
    nav.detailTxs.count = 2;
    tui::navHandle(nav, ftxui::Event::ArrowDown);
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Return) == NavAction::redraw);  // tx of that
                                                                                  // block
    BOOST_REQUIRE_EQUAL(nav.stack.size(), 2U);
    BOOST_CHECK(nav.stack[1].kind == DetailKind::tx);
    BOOST_CHECK_EQUAL(nav.stack[1].number, 1);  // index inside the block until resolved
    BOOST_CHECK(!nav.stack[1].resolved);
    tui::navHandle(nav, ftxui::Event::Escape);
    tui::navHandle(nav, ftxui::Event::Escape);
    BOOST_CHECK(!nav.inDetail());
    BOOST_CHECK_EQUAL(nav.blocks.selected, 2U);
    BOOST_CHECK(tui::navHandle(nav, ftxui::Event::Escape) == NavAction::none);  // empty stack:
                                                                                // no-op
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::ops::test
