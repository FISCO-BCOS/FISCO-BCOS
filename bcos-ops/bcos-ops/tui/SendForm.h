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
 * @brief state of the send page: the preset tree, the form fields of the selected entry, the
 *        focus area, the confirmation layer and the key map between them. Builds the
 *        SendRequest the Sender runs; holds no key and encodes nothing, so it is tested with
 *        bare events like Nav.
 * @file SendForm.h
 */
#pragma once

#include "Nav.h"
#include "Sender.h"
#include "bcos-ops/tx/FeatureFlags.h"
#include "bcos-ops/tx/Presets.h"
#include <array>
#include <ftxui/component/event.hpp>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace bcos::ops::tui
{
constexpr size_t c_sendMaxFields = 8;

enum class SendEntryKind
{
    group,  // a collapsible group row
    helloDeploy,
    preset,
    features,
    raw,
    custom
};

struct SendEntry
{
    SendEntryKind kind = SendEntryKind::group;
    std::string group;  // hello / consensus / … / features / raw / custom
    std::string label;  // row text
    Preset const* preset = nullptr;
};

/// one input of the form: `id` names the Input component ("to", "sig", "value", "raw",
/// "p0".."p7"); "features" is the checkbox list, not an input
struct SendField
{
    std::string id;
    std::string name;
    std::string type;
    std::string hint;
};

/// what the key map needs from the model (copied under the model mutex by the caller)
struct SendContext
{
    bool hasKey = false;
    bool routingKnown = false;
    bool proposals = false;
    std::string helloAddress;
    bool featuresLoaded = false;
    std::vector<FeatureState> features;
    bool busy = false;
};

struct SendForm
{
    bool elMode = false;  // only the raw entry exists (ticket 17)
    std::vector<SendEntry> entries;
    std::set<std::string> expanded;
    ListState tree;
    // 0 tree, 1 key file, 2 … fields (or the features list), then the button
    int focus = 0;
    std::string keyFile;
    std::array<std::string, c_sendMaxFields> fields;
    std::string rawText;
    std::string customTo;
    std::string customSig;
    std::string customValue;
    std::string helloTo;
    std::set<std::string> featureChecked;
    ListState featureList;
    bool confirm = false;
    SendRequest pending;
    std::vector<std::pair<std::string, std::string>> confirmRows;
    std::string notice;    // one-line feedback of the key map (e.g. "load a key first")
    std::string entryKey;  // the entry the fields belong to

    /// rebuilds `entries` from the preset table and `expanded`; keeps the cursor on the same row
    void rebuild();
    SendEntry const* current() const;
    std::vector<SendField> fieldSpecs() const;
    int fieldCount() const { return static_cast<int>(fieldSpecs().size()); }
    int buttonFocus() const { return 2 + fieldCount(); }
    /// the id of the focused input ("key", "to", "p0", …), "" when the focus is not on one
    std::string inputId() const;
    bool editing() const { return !inputId().empty(); }
    /// the string an input id edits (nullptr for "features" / unknown)
    std::string* valueOf(std::string const& _id);
    std::string const* valueOf(std::string const& _id) const;
};

enum class SendAction
{
    none,
    redraw,
    passToInput,  // the focused Input component takes the key
    toNav,        // not a form key: the page / list key map decides
    loadKey,      // form.keyFile → Sender
    submit,       // form.pending → Sender
    readFeatures  // the features entry needs Model.send.features
};

SendForm makeSendForm(bool _elMode);
/// moves the cursor / focus, toggles groups, opens and answers the confirmation layer
SendAction sendFormHandle(SendForm& _form, SendContext const& _context, ftxui::Event _event);
/// the request for the current entry from the field values; OpsError{1} names a missing value
SendRequest sendFormRequest(SendForm const& _form, SendContext const& _context);
/// the confirmation rows (mode / to / method / params …) the same way the CLI prints them
std::vector<std::pair<std::string, std::string>> sendConfirmRows(
    SendRequest const& _request, SendContext const& _context);
/// the proposal detail's key map (gov page, ticket 16): `v` vote for, `x` vote against, `d`
/// revoke open the confirmation layer of the committee preset for _proposalId; Enter in the
/// layer submits (form.pending), Esc cancels. No key loaded → form.notice, no layer. Other
/// keys → toNav
SendAction govFormHandle(
    SendForm& _form, SendContext const& _context, int64_t _proposalId, ftxui::Event _event);
}  // namespace bcos::ops::tui
