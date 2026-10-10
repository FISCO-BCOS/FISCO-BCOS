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
 * @brief the five views as pure render functions (Model → ftxui::Element) plus the interactive
 *        wrappers; render functions are what the tests call against an 80x24 screen
 * @file Views.h
 */
#pragma once

#include "bcos-ops/tui/Model.h"
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <functional>
#include <string>

namespace bcos::ops::tui
{
ftxui::Element renderTopBar(Model const& _model);
ftxui::Element renderOverview(Model const& _model);
ftxui::Element renderConsensus(Model const& _model);
ftxui::Element renderSync(Model const& _model);
ftxui::Element renderTx(Model const& _model);
ftxui::Element renderLog(Model const& _model, std::string const& _filter);

/// Tx view: a button that runs the smoke flow through _runSmoke (on a worker thread)
ftxui::Component makeTxView(Model& _model, std::function<void()> _runSmoke);
/// Log view: input box + filtered tail
ftxui::Component makeLogView(Model& _model, std::string& _filter);
}  // namespace bcos::ops::tui
