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
 * @file SendForm.cpp
 */
#include "SendForm.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/cmd/TxContext.h"
#include "bcos-ops/tx/FeatureFlags.h"
#include "bcos-ops/tx/PresetCall.h"
#include <algorithm>
#include <cctype>

namespace bcos::ops::tui
{
namespace
{
constexpr std::array c_sendGroups = {
    "hello", "consensus", "sysconfig", "balance", "auth", "committee", "features", "raw", "custom"};

std::string sendEntryKey(SendEntry const& _entry)
{
    return _entry.group + "/" + _entry.label;
}

void sendMove(ListState& _list, int64_t _delta)
{
    if (_list.count == 0)
    {
        _list.selected = 0;
        return;
    }
    auto target = std::clamp<int64_t>(
        static_cast<int64_t>(_list.selected) + _delta, 0, static_cast<int64_t>(_list.count) - 1);
    _list.selected = static_cast<size_t>(target);
}

/// the form's fields follow the entry under the cursor; a new entry starts empty, hello
/// set/get prefill the address of the HelloWorld deployed in this session
void sendSyncEntry(SendForm& _form, SendContext const& _context)
{
    auto const* entry = _form.current();
    auto key = entry ? sendEntryKey(*entry) : std::string();
    if (key == _form.entryKey)
    {
        return;
    }
    _form.entryKey = key;
    for (auto& field : _form.fields)
    {
        field.clear();
    }
    _form.customValue.clear();
    _form.notice.clear();
    if (entry && entry->kind == SendEntryKind::preset && entry->group == "hello")
    {
        _form.helloTo = _context.helloAddress;
    }
    if (entry && entry->kind == SendEntryKind::features)
    {
        _form.featureList.selected = 0;
    }
}
}  // namespace

void SendForm::rebuild()
{
    std::vector<SendEntry> rows;
    if (elMode)
    {
        rows.push_back({SendEntryKind::raw, "raw", "raw transaction", nullptr});
    }
    else
    {
        for (auto group : c_sendGroups)
        {
            std::string name(group);
            if (name == "features")
            {
                rows.push_back({SendEntryKind::features, name, "feature flags", nullptr});
                continue;
            }
            if (name == "raw")
            {
                rows.push_back({SendEntryKind::raw, name, "raw transaction", nullptr});
                continue;
            }
            if (name == "custom")
            {
                rows.push_back({SendEntryKind::custom, name, "custom call", nullptr});
                continue;
            }
            bool open = expanded.contains(name);
            rows.push_back({SendEntryKind::group, name, (open ? "- " : "+ ") + name, nullptr});
            if (!open)
            {
                continue;
            }
            if (name == "hello")
            {
                rows.push_back({SendEntryKind::helloDeploy, name, "  deploy HelloWorld", nullptr});
            }
            for (auto const* preset : presetsOf(name))
            {
                // the send page lists what writes; hello get is the one read, it shows the
                // value the deploy / set left behind
                if (!preset->write && !(name == "hello"))
                {
                    continue;
                }
                rows.push_back(
                    {SendEntryKind::preset, name, "  " + std::string(preset->name), preset});
            }
        }
    }
    entries = std::move(rows);
    tree.count = entries.size();
    if (tree.selected >= tree.count)
    {
        tree.selected = tree.count == 0 ? 0 : tree.count - 1;
    }
}

SendEntry const* SendForm::current() const
{
    if (entries.empty() || tree.selected >= entries.size())
    {
        return nullptr;
    }
    return &entries[tree.selected];
}

std::vector<SendField> SendForm::fieldSpecs() const
{
    std::vector<SendField> specs;
    auto const* entry = current();
    if (entry == nullptr)
    {
        return specs;
    }
    switch (entry->kind)
    {
    case SendEntryKind::group:
    case SendEntryKind::helloDeploy:
        break;
    case SendEntryKind::preset:
        if (entry->group == "hello")
        {
            specs.push_back({"to", "address", "address", "the HelloWorld to call"});
        }
        for (size_t i = 0; i < entry->preset->params.size() && i < c_sendMaxFields; ++i)
        {
            auto const& param = entry->preset->params[i];
            specs.push_back({"p" + std::to_string(i), std::string(param.name),
                std::string(param.type), std::string(param.hint)});
        }
        break;
    case SendEntryKind::features:
        specs.push_back({"features", "flags", "", "space toggles, button enables"});
        break;
    case SendEntryKind::raw:
        specs.push_back({"raw", "signed tx", "hex or @file",
            elMode ? "RLP-encoded signed Ethereum transaction" :
                     "tars-encoded signed transaction"});
        break;
    case SendEntryKind::custom:
    {
        specs.push_back({"to", "to", "address", ""});
        specs.push_back({"sig", "signature", "", "e.g. set(string)"});
        auto types =
            customSig.empty() ? std::vector<std::string>() : signatureInputTypes(customSig);
        for (size_t i = 0; i < types.size() && i < c_sendMaxFields; ++i)
        {
            specs.push_back({"p" + std::to_string(i), "arg" + std::to_string(i), types[i], ""});
        }
        specs.push_back({"value", "value", "wei", "optional"});
        break;
    }
    }
    return specs;
}

std::string SendForm::inputId() const
{
    if (focus == 1)
    {
        return "key";
    }
    auto specs = fieldSpecs();
    auto index = focus - 2;
    if (index < 0 || index >= static_cast<int>(specs.size()))
    {
        return "";
    }
    return specs[static_cast<size_t>(index)].id == "features" ?
               "" :
               specs[static_cast<size_t>(index)].id;
}

std::string* SendForm::valueOf(std::string const& _id)
{
    if (_id == "key")
    {
        return &keyFile;
    }
    if (_id == "raw")
    {
        return &rawText;
    }
    if (_id == "to")
    {
        auto const* entry = current();
        return entry && entry->kind == SendEntryKind::custom ? &customTo : &helloTo;
    }
    if (_id == "sig")
    {
        return &customSig;
    }
    if (_id == "value")
    {
        return &customValue;
    }
    if (_id.size() == 2 && _id[0] == 'p' && std::isdigit(static_cast<unsigned char>(_id[1])) != 0)
    {
        auto index = static_cast<size_t>(_id[1] - '0');
        return index < fields.size() ? &fields[index] : nullptr;
    }
    return nullptr;
}

std::string const* SendForm::valueOf(std::string const& _id) const
{
    return const_cast<SendForm*>(this)->valueOf(_id);
}

SendForm makeSendForm(bool _elMode)
{
    SendForm form;
    form.elMode = _elMode;
    form.expanded.insert("hello");
    form.rebuild();
    return form;
}

SendRequest sendFormRequest(SendForm const& _form, SendContext const& _context)
{
    (void)_context;
    auto const* entry = _form.current();
    if (entry == nullptr || entry->kind == SendEntryKind::group)
    {
        throw OpsError(c_exitUsage, "select a preset first");
    }
    SendRequest request;
    using Kind = SendRequest::Kind;
    switch (entry->kind)
    {
    case SendEntryKind::group:
        break;
    case SendEntryKind::helloDeploy:
        request.kind = Kind::helloDeploy;
        break;
    case SendEntryKind::preset:
    {
        request.kind = Kind::preset;
        request.preset = entry->preset;
        if (entry->group == "hello")
        {
            if (_form.helloTo.empty())
            {
                throw OpsError(c_exitUsage, "missing address: deploy HelloWorld first");
            }
            request.to = _form.helloTo;
        }
        for (size_t i = 0; i < entry->preset->params.size() && i < c_sendMaxFields; ++i)
        {
            if (_form.fields[i].empty())
            {
                throw OpsError(
                    c_exitUsage, "missing " + std::string(entry->preset->params[i].name));
            }
            request.params.push_back(_form.fields[i]);
        }
        break;
    }
    case SendEntryKind::features:
        request.kind = Kind::enableFeatures;
        request.features.assign(_form.featureChecked.begin(), _form.featureChecked.end());
        if (request.features.empty())
        {
            throw OpsError(c_exitUsage, "no flag selected");
        }
        break;
    case SendEntryKind::raw:
        request.kind = Kind::raw;
        request.raw = _form.rawText;
        request.web3Raw = _form.elMode;
        if (request.raw.empty())
        {
            throw OpsError(c_exitUsage, "missing signed transaction");
        }
        break;
    case SendEntryKind::custom:
    {
        request.kind = Kind::custom;
        request.to = _form.customTo;
        request.signature = _form.customSig;
        request.value = _form.customValue;
        if (request.to.empty() || request.signature.empty())
        {
            throw OpsError(c_exitUsage, "missing to or signature");
        }
        auto types = signatureInputTypes(_form.customSig);
        // the form has c_sendMaxFields inputs; sending fewer arguments than the signature
        // declares would revert or hit another overload, so refuse instead of truncating
        if (types.size() > c_sendMaxFields)
        {
            throw OpsError(c_exitUsage, "custom calls here take at most " +
                                            std::to_string(c_sendMaxFields) +
                                            " arguments; use `fisco-bcos tx send` for more");
        }
        for (size_t i = 0; i < types.size(); ++i)
        {
            if (_form.fields[i].empty())
            {
                throw OpsError(c_exitUsage, "missing arg" + std::to_string(i));
            }
            request.params.push_back(_form.fields[i]);
        }
        break;
    }
    }
    return request;
}

std::vector<std::pair<std::string, std::string>> sendConfirmRows(
    SendRequest const& _request, SendContext const& _context)
{
    using Kind = SendRequest::Kind;
    std::vector<std::pair<std::string, std::string>> rows;
    switch (_request.kind)
    {
    case Kind::preset:
    {
        auto const& preset = *_request.preset;
        auto const& routing = _request.routing;  // the decision the worker will send
        bool viaProposal = PresetRunner::routesThroughProposal(routing, preset);
        auto const* committee = viaProposal ? PresetRunner::proposalPreset(preset) : nullptr;
        if (viaProposal && committee != nullptr)
        {
            return confirmRows(*committee, std::string(committee->to),
                proposalArgsFor(preset, _request.params, routing), "proposal",
                "stands in for " + std::string(preset.signature));
        }
        auto to = _request.to.empty() ? std::string(preset.to) : _request.to;
        return confirmRows(preset, to, _request.params, preset.write ? "direct" : "call");
    }
    case Kind::helloDeploy:
        rows = {{"mode", "direct"}, {"to", "(new contract)"}, {"method", "HelloWorld()"},
            {"params", ""}};
        break;
    case Kind::custom:
    {
        std::string params;
        for (size_t i = 0; i < _request.params.size(); ++i)
        {
            params += (i == 0 ? "" : ", ") + _request.params[i];
        }
        rows = {{"mode", "direct"}, {"to", _request.to}, {"method", _request.signature},
            {"params", params}};
        if (!_request.value.empty())
        {
            rows.emplace_back("value", _request.value);
        }
        break;
    }
    case Kind::raw:
        rows = {{"mode", "raw"},
            {"to", _request.web3Raw ? "eth_sendRawTransaction (web3)" : "sendTransaction"},
            {"method", _request.raw.starts_with("@") ? "file " + _request.raw.substr(1) :
                                                       "signed transaction"},
            {"params", std::to_string(_request.raw.size()) + " characters"}};
        break;
    case Kind::enableFeatures:
    {
        // the steps the CLI's enable-features shows: dependency order with the flags it adds
        std::vector<std::string> order;
        try
        {
            order = featureEnableOrder(_request.features);
        }
        catch (OpsError const&)
        {
            order = _request.features;
        }
        // flags the page already read as on are skipped by enableFeatures; they are not steps
        std::string params;
        size_t steps = 0;
        std::string skipped;
        for (auto const& name : order)
        {
            auto state = std::find_if(_context.features.begin(), _context.features.end(),
                [&](FeatureState const& f) { return f.name == name; });
            if (state != _context.features.end() && state->enabled)
            {
                skipped += (skipped.empty() ? "" : ", ") + name;
                continue;
            }
            params += (steps++ == 0 ? "" : ", ") + name + "=1";
        }
        auto const* preset = findPreset("sysconfig", "set");
        bool viaProposal = PresetRunner::routesThroughProposal(_request.routing, *preset);
        auto const* committee = viaProposal ? PresetRunner::proposalPreset(*preset) : nullptr;
        // what is sent: one createSetSysConfigProposal per flag to the committee in proposal
        // mode, one setValueByKey per flag to 0x…1000 otherwise
        auto const* sent = committee != nullptr ? committee : preset;
        rows = {{"mode", viaProposal ? "proposal" : "direct"}, {"to", std::string(sent->to)},
            {"method", std::string(sent->signature)},
            {"params", std::to_string(steps) + " step(s): " + params}};
        if (!skipped.empty())
        {
            rows.emplace_back("already on", skipped);
        }
        if (committee != nullptr)
        {
            rows.emplace_back("note", std::string(committee->signature) + " stands in for " +
                                          std::string(preset->signature) + ", one per flag");
        }
        break;
    }
    case Kind::loadKey:
    case Kind::readFeatures:
        break;
    }
    return rows;
}

SendAction sendFormHandle(SendForm& _form, SendContext const& _context, ftxui::Event _event)
{
    using ftxui::Event;
    if (_form.entries.empty())
    {
        _form.rebuild();
    }
    sendSyncEntry(_form, _context);
    if (_event == Event::CtrlC)
    {
        return SendAction::toNav;
    }
    if (_form.confirm)
    {
        if (_event == Event::Return)
        {
            _form.confirm = false;
            _form.focus = 0;  // the outcome opens the tx detail; Esc then lands on the tree
            return SendAction::submit;
        }
        if (_event == Event::Escape)
        {
            _form.confirm = false;
            _form.notice = "cancelled";
            return SendAction::redraw;
        }
        return SendAction::none;
    }
    auto areas = _form.buttonFocus() + 1;
    if (_event == Event::Tab || _event == Event::TabReverse)
    {
        _form.focus =
            _event == Event::Tab ? (_form.focus + 1) % areas : (_form.focus + areas - 1) % areas;
        return SendAction::redraw;
    }
    if (_event == Event::Escape)
    {
        if (_form.focus != 0)
        {
            _form.focus = 0;
            return SendAction::redraw;
        }
        return SendAction::toNav;
    }
    if (_form.editing())
    {
        if (_event == Event::Return)
        {
            auto id = _form.inputId();
            if (id == "key")
            {
                if (_form.keyFile.empty())
                {
                    _form.notice = "key file: enter a PEM or hex private key path";
                    return SendAction::redraw;
                }
                return SendAction::loadKey;
            }
            if (id == "raw")
            {
                return SendAction::passToInput;  // a newline in the raw box
            }
            _form.focus = _form.buttonFocus();
            return SendAction::redraw;
        }
        return SendAction::passToInput;
    }
    auto const* entry = _form.current();
    bool onFeatureList = entry && entry->kind == SendEntryKind::features && _form.focus == 2;
    if (_form.focus == 0 || onFeatureList)
    {
        auto& list = onFeatureList ? _form.featureList : _form.tree;
        if (onFeatureList)
        {
            list.count = _context.features.size();
        }
        int64_t delta = 0;
        if (_event == Event::ArrowDown || _event == Event::Character('j'))
        {
            delta = 1;
        }
        else if (_event == Event::ArrowUp || _event == Event::Character('k'))
        {
            delta = -1;
        }
        else if (_event == Event::PageDown)
        {
            delta = static_cast<int64_t>(std::max<size_t>(1, list.pageRows));
        }
        else if (_event == Event::PageUp)
        {
            delta = -static_cast<int64_t>(std::max<size_t>(1, list.pageRows));
        }
        if (delta != 0)
        {
            sendMove(list, delta);
            if (!onFeatureList)
            {
                sendSyncEntry(_form, _context);
                auto const* now = _form.current();
                if (now && now->kind == SendEntryKind::features && !_context.featuresLoaded &&
                    !_context.busy)
                {
                    return SendAction::readFeatures;
                }
            }
            return SendAction::redraw;
        }
        if (onFeatureList && (_event == Event::Character(' ') || _event == Event::Return))
        {
            if (list.selected < _context.features.size())
            {
                auto const& flag = _context.features[list.selected];
                if (flag.enabled)
                {
                    _form.notice = flag.name + " is already on";
                }
                else if (!_form.featureChecked.erase(flag.name))
                {
                    _form.featureChecked.insert(flag.name);
                }
            }
            return SendAction::redraw;
        }
        if (!onFeatureList && entry != nullptr &&
            (_event == Event::Return || _event == Event::Character(' ') ||
                _event == Event::ArrowRight || _event == Event::ArrowLeft))
        {
            if (entry->kind == SendEntryKind::group)
            {
                if (!_form.expanded.erase(entry->group))
                {
                    _form.expanded.insert(entry->group);
                }
                _form.rebuild();
                return SendAction::redraw;
            }
            if (_event == Event::Return)
            {
                _form.focus = _form.fieldCount() > 0 ? 2 : _form.buttonFocus();
                if (entry->kind == SendEntryKind::features && !_context.featuresLoaded)
                {
                    return SendAction::readFeatures;
                }
                return SendAction::redraw;
            }
        }
        return SendAction::toNav;
    }
    if (_form.focus == _form.buttonFocus())
    {
        if (_event == Event::Return || _event == Event::Character(' '))
        {
            try
            {
                auto request = sendFormRequest(_form, _context);
                if (request.needsKey() && !_context.hasKey)
                {
                    _form.notice = "load a key first";
                    return SendAction::redraw;
                }
                if (_context.busy)
                {
                    _form.notice = "a request is still running";
                    return SendAction::redraw;
                }
                // the layer shows direct | proposal; it must know, not guess
                if (SendRequest::mayRouteThroughProposal(request.kind, request.preset) &&
                    !_context.routingKnown)
                {
                    _form.notice = "routing unknown: reload the key";
                    return SendAction::redraw;
                }
                request.routing.proposals = _context.proposals;
                _form.pending = request;
                _form.confirmRows = sendConfirmRows(request, _context);
                _form.confirm = true;
                _form.notice.clear();
            }
            catch (OpsError const& e)
            {
                _form.notice = e.what();
            }
            return SendAction::redraw;
        }
        return SendAction::toNav;
    }
    return SendAction::toNav;
}
SendAction govFormHandle(
    SendForm& _form, SendContext const& _context, int64_t _proposalId, ftxui::Event _event)
{
    using ftxui::Event;
    if (_event == Event::CtrlC)
    {
        return SendAction::toNav;
    }
    if (_form.confirm)
    {
        if (_event == Event::Return)
        {
            _form.confirm = false;
            return SendAction::submit;
        }
        if (_event == Event::Escape)
        {
            _form.confirm = false;
            _form.notice = "cancelled";
            return SendAction::redraw;
        }
        return SendAction::none;
    }
    if (!_event.is_character() || _event.character().size() != 1)
    {
        return SendAction::toNav;
    }
    char key = _event.character()[0];
    if (key != 'v' && key != 'x' && key != 'd')
    {
        return SendAction::toNav;
    }
    if (!_context.hasKey)
    {
        _form.notice = "load a key first (tui --key <file>, or the send page's key file)";
        return SendAction::redraw;
    }
    if (_context.busy)
    {
        _form.notice = "a request is still running";
        return SendAction::redraw;
    }
    if (_context.proposalStatus != 1)
    {
        _form.notice = "proposal is not voting (" + proposalStatusName(_context.proposalStatus) +
                       "): nothing to vote on or revoke";
        return SendAction::redraw;
    }
    SendRequest request;
    request.kind = SendRequest::Kind::preset;
    request.openTxDetail = false;  // the proposal page stays; its rows refresh next round
    request.routing.proposals = _context.proposals;  // vote/revoke have no proposal form
    if (key == 'd')
    {
        request.preset = findPreset("committee", "revoke");
        request.params = {std::to_string(_proposalId)};
    }
    else
    {
        request.preset = findPreset("committee", "vote");
        request.params = {std::to_string(_proposalId), key == 'v' ? "true" : "false"};
    }
    _form.pending = request;
    _form.confirmRows = sendConfirmRows(request, _context);
    _form.confirm = true;
    _form.notice.clear();
    return SendAction::redraw;
}
bool sendEntryNeedsKey(SendEntry const& _entry)
{
    SendRequest probe;
    switch (_entry.kind)
    {
    case SendEntryKind::group:
        return false;
    case SendEntryKind::helloDeploy:
        probe.kind = SendRequest::Kind::helloDeploy;
        break;
    case SendEntryKind::preset:
        probe.kind = SendRequest::Kind::preset;
        probe.preset = _entry.preset;
        break;
    case SendEntryKind::features:
        probe.kind = SendRequest::Kind::enableFeatures;
        break;
    case SendEntryKind::raw:
        probe.kind = SendRequest::Kind::raw;
        break;
    case SendEntryKind::custom:
        probe.kind = SendRequest::Kind::custom;
        break;
    }
    return probe.needsKey();
}
void dropConfirmOnLeave(
    SendForm& _form, Nav const& _nav, Page _pageBefore, size_t _stackDepthBefore)
{
    if (_nav.page != _pageBefore || _nav.stack.size() < _stackDepthBefore)
    {
        _form.confirm = false;
    }
}
}  // namespace bcos::ops::tui
