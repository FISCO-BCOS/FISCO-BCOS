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
 * @brief the send page: preset tree, the selected entry's form, the key file box, the send
 *        button and the last outcome; plus the confirmation layer. Draws the Input components
 *        the frame hands in, or the plain values when headless.
 * @file Send.cpp
 */
#include "Pages.h"
#include "bcos-ops/Output.h"
#include <algorithm>

namespace bcos::ops::tui
{
using namespace ftxui;

namespace
{
constexpr int c_sendTreeWidth = 26;

Element sendInputOrText(
    Frame const& _frame, std::string const& _id, std::string const& _value, bool _focused)
{
    auto it = _frame.inputs.find(_id);
    if (it != _frame.inputs.end() && it->second)
    {
        return it->second;
    }
    auto element = text(_value.empty() ? " " : sanitizeForTerminal(_value));
    return _focused ? element | inverted : element;
}

Element sendTree(SendForm& _form, int _height)
{
    _form.tree.count = _form.entries.size();
    _form.tree.pageRows = static_cast<size_t>(std::max(1, _height));
    auto first = _form.tree.firstVisible();
    Elements rows;
    for (size_t i = first; i < _form.entries.size() && rows.size() < _form.tree.pageRows; ++i)
    {
        auto const& entry = _form.entries[i];
        auto row = text(entry.label);
        if (entry.kind == SendEntryKind::group)
        {
            row = row | bold;
        }
        if (i == _form.tree.selected)
        {
            row = _form.focus == 0 ? row | inverted : row | bold;
        }
        rows.push_back(row);
    }
    if (rows.empty())
    {
        rows.push_back(text("no presets") | dim);
    }
    return vbox(std::move(rows));
}

Element sendFeatureList(Model const& _model, SendForm& _form, int _height, bool _focused)
{
    auto const& flags = _model.send.features;
    _form.featureList.count = flags.size();
    _form.featureList.pageRows = static_cast<size_t>(std::max(1, _height));
    if (!_model.send.featuresLoaded)
    {
        return text(_model.send.busy ? "reading feature flags…" : "feature flags not read yet") |
               dim;
    }
    auto first = _form.featureList.firstVisible();
    Elements rows;
    for (size_t i = first; i < flags.size() && rows.size() < _form.featureList.pageRows; ++i)
    {
        auto const& flag = flags[i];
        std::string box =
            flag.enabled ? "[on]" : (_form.featureChecked.contains(flag.name) ? "[x] " : "[ ] ");
        auto row = hbox({text(box + " " + flag.name + " "),
            flag.enabled ? text("on") | color(Color::Green) : text("off") | dim,
            flag.deprecated ? text(" deprecated") | dim : text("")});
        if (i == _form.featureList.selected && _focused)
        {
            row = row | inverted;
        }
        rows.push_back(row);
    }
    return vbox(std::move(rows));
}

Element sendButton(SendForm const& _form, SendContext const& _context, bool _focused)
{
    auto const* entry = _form.current();
    std::string label = " send ";
    if (entry && entry->kind == SendEntryKind::features)
    {
        label = " enable selected ";
    }
    else if (entry && entry->kind == SendEntryKind::preset && !entry->preset->write)
    {
        label = " call ";
    }
    bool needsKey = entry != nullptr && sendEntryNeedsKey(*entry);
    if (needsKey && !_context.hasKey)
    {
        auto button = text("[" + label + "]") | dim;
        return hbox({_focused ? button | inverted : button, text("  load a key first") | dim});
    }
    auto button = text("[" + label + "]") | bold;
    if (_context.busy)
    {
        return hbox({button | dim, text("  sending…") | dim});
    }
    return _focused ? button | inverted : button;
}

Element sendRightPane(Model const& _model, SendForm& _form, Frame const& _frame)
{
    SendContext context;
    context.hasKey = !_model.keyAddress.empty();
    context.busy = _model.send.busy;
    auto const* entry = _form.current();
    Elements rows;
    // key file box
    rows.push_back(hbox({text("key file: ") | (_form.focus == 1 ? bold : dim),
        sendInputOrText(_frame, "key", _form.keyFile, _form.focus == 1) | flex}));
    rows.push_back(hbox({text("key:      ") | dim,
        _model.keyAddress.empty() ? text("none") | dim : text(_model.keyAddress)}));
    rows.push_back(separatorLight());
    auto specs = _form.fieldSpecs();
    int listHeight = std::max(1, _frame.height - 10);
    if (entry && entry->kind == SendEntryKind::group)
    {
        rows.push_back(text("Enter opens the group; pick a preset") | dim);
    }
    else if (entry && entry->kind == SendEntryKind::helloDeploy)
    {
        rows.push_back(text("deploys the embedded HelloWorld (constructor without arguments)"));
        if (!_model.send.helloAddress.empty())
        {
            rows.push_back(hbox({text("last deployed: ") | dim,
                text(sanitizeForTerminal(_model.send.helloAddress))}));
        }
    }
    for (size_t i = 0; i < specs.size(); ++i)
    {
        auto const& spec = specs[i];
        bool focused = _form.focus == static_cast<int>(2 + i);
        if (spec.id == "features")
        {
            rows.push_back(sendFeatureList(_model, _form, listHeight, focused));
            continue;
        }
        auto label = spec.name + (spec.type.empty() ? "" : " (" + spec.type + ")") + ": ";
        auto const* value = _form.valueOf(spec.id);
        auto field = sendInputOrText(_frame, spec.id, value ? *value : "", focused);
        if (spec.id == "raw")
        {
            rows.push_back(text(label) | (focused ? bold : dim));
            rows.push_back(field | size(HEIGHT, GREATER_THAN, 3) | flex);
        }
        else
        {
            rows.push_back(hbox({text(label) | (focused ? bold : dim), field | flex}));
        }
        if (!spec.hint.empty())
        {
            rows.push_back(hbox({text(std::string(label.size(), ' ')), text(spec.hint) | dim}));
        }
    }
    if (entry && entry->kind != SendEntryKind::group)
    {
        rows.push_back(separatorLight());
        rows.push_back(sendButton(_form, context, _form.focus == _form.buttonFocus()));
    }
    if (!_form.notice.empty())
    {
        rows.push_back(text(sanitizeForTerminal(_form.notice)) | color(Color::Yellow));
    }
    return vbox(std::move(rows));
}
}  // namespace

Element renderSend(Model const& _model, SendForm& _form, Frame const& _frame)
{
    if (_form.entries.empty())
    {
        _form.rebuild();
    }
    auto const* entry = _form.current();
    std::string title =
        entry ? " " + entry->group +
                    (entry->kind == SendEntryKind::group ? "" : ": " + entry->label) + " " :
                " send ";
    while (title.find("  ") != std::string::npos)
    {
        title.replace(title.find("  "), 2, " ");
    }
    int bodyHeight = std::max(1, _frame.height - 3);
    auto left = window(text(" presets "), sendTree(_form, bodyHeight - 2), LIGHT) |
                size(WIDTH, EQUAL, c_sendTreeWidth);
    auto right = window(text(title), sendRightPane(_model, _form, _frame), LIGHT) | flex;
    auto const& last = _model.send.last;
    Element status;
    if (last.seq == 0)
    {
        status = text(" no transaction sent yet") | dim;
    }
    else
    {
        status =
            hbox({text(last.ok ? " ok " : " FAIL ") | color(last.ok ? Color::Green : Color::Red),
                text(sanitizeForTerminal(last.line))});
    }
    Element body =
        _frame.width >= 70 ? hbox({left, right}) : vbox({left | size(HEIGHT, LESS_THAN, 8), right});
    return vbox({body | flex, status});
}

Element renderSendConfirm(SendForm const& _form)
{
    Elements lines;
    size_t width = 0;
    for (auto const& [label, value] : _form.confirmRows)
    {
        width = std::max(width, label.size());
    }
    for (auto const& [label, value] : _form.confirmRows)
    {
        lines.push_back(hbox({text(label + std::string(width + 1 - label.size(), ' ')) | dim,
            paragraph(sanitizeForTerminal(value))}));
    }
    lines.push_back(text(""));
    lines.push_back(text("Enter send   Esc cancel") | dim);
    return window(text(" confirm "), vbox(std::move(lines)) | size(WIDTH, LESS_THAN, 90), LIGHT);
}
}  // namespace bcos::ops::tui
