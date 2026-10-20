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
 * @brief `fisco-bcos tui`: assembles the model, the refresher and log-tail threads, the Nav key
 *        map and the page renderers into one FTXUI loop. --snapshot-dir renders every page
 *        headlessly; --version-check only proves the library links.
 * @file TuiCmd.cpp
 */
#include "bcos-ops/Args.h"
#include "bcos-ops/Cli.h"
#include "bcos-ops/Connect.h"
#include "bcos-ops/Mode.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/cmd/StatusCmd.h"
#include "bcos-ops/collect/RpcCollector.h"
#include "bcos-ops/tui/Explorer.h"
#include "bcos-ops/tui/LogTail.h"
#include "bcos-ops/tui/Model.h"
#include "bcos-ops/tui/Nav.h"
#include "bcos-ops/tui/Refresher.h"
#include "bcos-ops/tui/SendForm.h"
#include "bcos-ops/tui/Sender.h"
#include "bcos-ops/tui/views/Pages.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/screen/screen.hpp>
#include <ostream>
#include <thread>

namespace bcos::ops
{
namespace
{
using namespace ftxui;  // bcos::ops::Event clashes with ftxui::Event, which is always qualified

int64_t tuiNowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count();
}

LocalFallbacks tuiFallbacks(Connection const& _connection)
{
    LocalFallbacks fallbacks;
    if (_connection.nodeDir)
    {
        fallbacks.txpoolLimit = static_cast<int64_t>(_connection.nodeDir->txpoolLimit);
        fallbacks.consensusTimeoutMs = _connection.nodeDir->consensusTimeoutMs;
    }
    return fallbacks;
}

void tuiAppendLog(tui::Model& _model, std::vector<bcos::ops::Event> _events)
{
    std::lock_guard<std::mutex> lock(_model.mutex);
    for (auto& event : _events)
    {
        _model.logTail.push_back(std::move(event));
    }
    while (_model.logTail.size() > 500)
    {
        _model.logTail.pop_front();
    }
}

/// --snapshot-dir: every page from live data into <dir>/<n>-<page>.txt (home at 80x24 and
/// 120x40, the others at 80x24) for documentation and CI where no terminal is attached
int snapshotPages(
    Connection const& _connection, Args const& _args, std::string const& _dir, std::ostream& _out)
{
    tui::Model model;
    model.mode = _connection.mode;
    bool el = isElMode(_connection.mode);
    tui::Refresher refresher(
        _connection, tuiFallbacks(_connection), thresholdsFrom(_args), model, nullptr, 100000);
    refresher.setRowCapacity([]() { return tui::explorerCapacity(40); });
    refresher.setVisibleTxRange([]() { return std::make_pair(size_t(0), size_t(40)); });
    refresher.setGovernanceWants([]() { return tui::GovernanceWants{.rows = 20, .ids = {}}; });
    refresher.runOnce();
    if (_connection.nodeDir)
    {
        tui::LogTail tail(_connection.nodeDir->logDir());
        tuiAppendLog(model, tail.poll());
    }
    std::filesystem::create_directories(_dir);
    std::string filter = _args.optionOr("filter", "");
    struct Shot
    {
        std::string name;
        int width;
        int height;
        std::function<void(tui::Nav&, tui::SendForm&)> setup;
    };
    auto onPage = [](tui::Page page) {
        return [page](tui::Nav& nav, tui::SendForm&) { nav.page = page; };
    };
    auto noSetup = [](tui::Nav&, tui::SendForm&) {};
    // the send page with consensus add-sealer selected and no key; then its confirmation layer
    auto sendSetup = [el](tui::Nav& nav, tui::SendForm& form) {
        nav.page = tui::Page::send;
        form = tui::makeSendForm(el);
        if (el)
        {
            // EL modes: the raw entry is the whole tree
            form.rebuild();
            form.rawText = "0x02f8…";
            return;
        }
        form.expanded = {"consensus"};
        form.rebuild();
        for (size_t i = 0; i < form.entries.size(); ++i)
        {
            if (form.entries[i].preset && form.entries[i].preset->name == "add-sealer")
            {
                form.tree.selected = i;
            }
        }
        form.fields[0] = "3a1f00…";
        form.fields[1] = "1";
    };
    auto sendConfirmSetup = [sendSetup](tui::Nav& nav, tui::SendForm& form) {
        sendSetup(nav, form);
        form.focus = form.buttonFocus();
        tui::SendContext context;
        context.hasKey = true;
        form.pending = tui::sendFormRequest(form, context);
        form.confirmRows = tui::sendConfirmRows(form.pending, context);
        // the interactive page opens this layer only with a key loaded; the headless render
        // has none, so it says so instead of pretending one is there
        form.confirmRows.insert(
            form.confirmRows.begin(), {"key", "(snapshot: a loaded key is assumed)"});
        form.confirm = true;
    };
    std::vector<Shot> shots = {
        {.name = "home", .width = 80, .height = 24, .setup = noSetup},
        {.name = "home-wide", .width = 120, .height = 40, .setup = noSetup},
        {.name = el ? "txpool" : "consensus",
            .width = 80,
            .height = 24,
            .setup = onPage(tui::Page::consensus)},
        {.name = "node", .width = 80, .height = 24, .setup = onPage(tui::Page::node)},
        {.name = "log", .width = 80, .height = 24, .setup = onPage(tui::Page::log)},
        {.name = "help",
            .width = 80,
            .height = 24,
            .setup = [](tui::Nav& nav, tui::SendForm&) { nav.help = true; }},
        // the detail pages: the newest block and the newest transaction (when there is one)
        {.name = "block",
            .width = 80,
            .height = 24,
            .setup =
                [](tui::Nav& nav, tui::SendForm&) {
                    nav.stack.push_back({.kind = tui::DetailKind::block, .number = 0});
                }},
        {.name = "tx",
            .width = 80,
            .height = 24,
            .setup =
                [](tui::Nav& nav, tui::SendForm&) {
                    nav.stack.push_back({.kind = tui::DetailKind::tx, .number = 0});
                }},
        {.name = "send", .width = 120, .height = 40, .setup = sendSetup},
        {.name = "send-confirm", .width = 120, .height = 40, .setup = sendConfirmSetup},
        // the gov page and the newest proposal (auth chains; a note elsewhere)
        {.name = "gov", .width = 80, .height = 24, .setup = onPage(tui::Page::gov)},
        {.name = "proposal",
            .width = 80,
            .height = 24,
            .setup =
                [](tui::Nav& nav, tui::SendForm&) {
                    nav.page = tui::Page::gov;
                    nav.stack.push_back({.kind = tui::DetailKind::proposal, .number = 0});
                }},
    };
    int index = 1;
    for (auto const& shot : shots)
    {
        tui::Nav nav;
        tui::SendForm form = tui::makeSendForm(false);
        shot.setup(nav, form);
        if (nav.inDetail())
        {
            std::vector<tui::DetailWant> wants;
            tui::GovernanceWants govWants;
            {
                std::lock_guard<std::mutex> lock(model.mutex);
                tui::resolveDetailRefs(nav, model.explorer);
                tui::resolveProposalRefs(nav, model.governance);
                wants = tui::detailWants(nav);
                govWants = tui::governanceWants(nav);
            }
            refresher.setDetailWants([wants]() { return wants; });
            refresher.setGovernanceWants([govWants]() { return govWants; });
            refresher.runOnce();
        }
        auto screen = ftxui::Screen::Create(
            ftxui::Dimension::Fixed(shot.width), ftxui::Dimension::Fixed(shot.height));
        {
            std::lock_guard<std::mutex> lock(model.mutex);
            tui::Frame frame{.width = shot.width, .height = shot.height, .nowMs = tuiNowMs()};
            ftxui::Render(screen, tui::renderFrame(model, nav, frame, filter, &form));
        }
        auto path = _dir + "/" + std::to_string(index++) + "-" + shot.name + ".txt";
        std::ofstream out(path);
        out << screen.ToString();
        _out << path << '\n';
    }
    return c_exitOk;
}

int runTui(Args const& _args, std::ostream& _out, std::ostream& _err)
{
    (void)_err;
    if (_args.flag("version-check"))
    {
        // CI smoke: the TUI library links and the command dispatches, without entering the terminal
        _out << "ftxui linked; tui available\n";
        return c_exitOk;
    }
    auto connection = connect(connectOptionsFrom(_args));
    if (auto snapshotDir = _args.option("snapshot-dir"))
    {
        return snapshotPages(connection, _args, *snapshotDir, _out);
    }
    tui::Model model;
    model.mode = connection.mode;
    tui::Nav nav;
    tui::SendForm form = tui::makeSendForm(isElMode(connection.mode));
    auto screen = ScreenInteractive::Fullscreen();
    auto redraw = [&screen]() { screen.PostEvent(ftxui::Event::Custom); };
    // the send page's worker owns the signing key; --key (or --account) loads it before the loop.
    // It gets its own connection: the local socket serves one request at a time, and a send
    // must not queue behind a refresh round of up to 50 block fetches. Its calls check the stop
    // flag like the refresher's
    tui::Sender sender(connect(connectOptionsFrom(_args)), model, redraw);
    if (auto key = _args.option("key"); key || _args.option("account"))
    {
        tui::SendRequest load;
        load.kind = tui::SendRequest::Kind::loadKey;
        load.keyFile = key ? *key : *_args.option("account");
        form.keyFile = load.keyFile;
        sender.submit(std::move(load));
    }
    // the refresher thread reads these; the UI thread writes them while rendering
    std::atomic<int> terminalRows{24};
    std::atomic<size_t> visibleTxFrom{0};
    std::atomic<size_t> visibleTxCount{20};
    tui::Refresher refresher(
        connection, tuiFallbacks(connection), thresholdsFrom(_args), model, redraw, 2000);
    refresher.setRowCapacity([&]() { return tui::explorerCapacity(terminalRows.load()); });
    refresher.setVisibleTxRange(
        [&]() { return std::make_pair(visibleTxFrom.load(), visibleTxCount.load()); });
    // the open detail pages: written by the UI thread after each render, read by the refresher
    std::mutex wantsMutex;
    std::vector<tui::DetailWant> wants;
    tui::GovernanceWants govWants;
    refresher.setDetailWants([&]() {
        std::lock_guard<std::mutex> lock(wantsMutex);
        return wants;
    });
    refresher.setGovernanceWants([&]() {
        std::lock_guard<std::mutex> lock(wantsMutex);
        return govWants;
    });

    // log tail thread: 1 s
    std::atomic<bool> stopTail{false};
    std::thread tailThread;
    if (connection.nodeDir)
    {
        tailThread = std::thread([&model, &stopTail, redraw, dir = connection.nodeDir->logDir()]() {
            tui::LogTail tail(dir);
            while (!stopTail)
            {
                auto events = tail.poll();
                if (!events.empty())
                {
                    tuiAppendLog(model, std::move(events));
                    redraw();
                }
                for (int i = 0; i < 10 && !stopTail; ++i)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        });
    }
    auto stopWorkers = [&]() {
        refresher.stop();
        sender.stop();
        stopTail = true;
        if (tailThread.joinable())
        {
            tailThread.join();
        }
    };

    std::string filter;
    auto filterInput = Input(&filter, "event name or channel (e.g. ViewChange, TXPOOL)");
    // the send page's inputs: one per field id, all created once (the form only shows the ones
    // its entry has); Container::Vertical routes keys to the one sendSelector points at
    std::vector<std::string> sendIds = {"key", "to", "sig", "value", "raw"};
    for (size_t i = 0; i < tui::c_sendMaxFields; ++i)
    {
        sendIds.push_back("p" + std::to_string(i));
    }
    std::map<std::string, Component> sendInputs;
    Components sendChildren;
    for (auto const& id : sendIds)
    {
        InputOption option;
        option.multiline = id == "raw";
        option.placeholder = id == "key" ? "path to a PEM or hex private key" :
                             id == "raw" ? "signed tx hex, or @file" :
                                           "";
        auto component =
            id == "key"   ? Input(&form.keyFile, option) :
            id == "raw"   ? Input(&form.rawText, option) :
            id == "to"    ? Input(&form.customTo, option) :
            id == "sig"   ? Input(&form.customSig, option) :
            id == "value" ? Input(&form.customValue, option) :
                            Input(&form.fields[static_cast<size_t>(id[1] - '0')], option);
        sendInputs[id] = component;
        sendChildren.push_back(component);
    }
    auto helloToInput = Input(&form.helloTo, "");  // hello set/get address
    sendChildren.push_back(helloToInput);
    int sendSelector = 0;
    auto sendContainer = Container::Vertical(sendChildren, &sendSelector);
    int inputTab = 0;  // 0: the log filter, 1: the send page
    auto inputs = Container::Tab({filterInput, sendContainer}, &inputTab);
    auto sendInputFor = [&](std::string const& id) -> Component {
        auto const* entry = form.current();
        if (id == "to" && entry && entry->kind != tui::SendEntryKind::custom)
        {
            return helloToInput;
        }
        auto it = sendInputs.find(id);
        return it == sendInputs.end() ? nullptr : it->second;
    };
    auto sendContextOf = [&]() {
        // under model.mutex
        tui::SendContext context;
        context.hasKey = !model.keyAddress.empty();
        context.routingKnown = model.send.routingKnown;
        context.proposals = model.send.proposals;
        context.helloAddress = model.send.helloAddress;
        context.featuresLoaded = model.send.featuresLoaded;
        context.features = model.send.features;
        context.busy = model.send.busy;
        return context;
    };
    // FTXUI drains the whole event queue before it draws, so the Input a key lands in must be
    // chosen when the key arrives, not at the next render (Tab + a character in one batch)
    auto syncSendSelector = [&]() {
        auto focused = form.inputId();
        if (focused.empty())
        {
            return;
        }
        auto component = sendInputFor(focused);
        for (size_t i = 0; i < sendChildren.size(); ++i)
        {
            if (sendChildren[i] == component)
            {
                sendSelector = static_cast<int>(i);
            }
        }
    };
    uint64_t seenOutcome = 0;
    auto root = Renderer(inputs, [&]() {
        std::lock_guard<std::mutex> lock(model.mutex);
        terminalRows = screen.dimy();
        bool editingFilter = !nav.inDetail() && nav.page == tui::Page::log && nav.focus == 1;
        bool onSend = !nav.inDetail() && nav.page == tui::Page::send;
        inputTab = onSend ? 1 : 0;
        tui::Frame frame{.width = screen.dimx(),
            .height = screen.dimy(),
            .nowMs = tuiNowMs(),
            .filterInput = editingFilter ? filterInput->Render() : nullptr};
        if (onSend)
        {
            for (auto const& spec : form.fieldSpecs())
            {
                if (auto component = sendInputFor(spec.id))
                {
                    frame.inputs[spec.id] = component->Render();
                }
            }
            frame.inputs["key"] = sendInputs["key"]->Render();
            syncSendSelector();
        }
        auto element = tui::renderFrame(model, nav, frame, filter, &form);
        visibleTxFrom = nav.txs.firstVisible();
        visibleTxCount = nav.txs.pageRows;
        {
            std::lock_guard<std::mutex> wantsLock(wantsMutex);
            wants = tui::detailWants(nav);
            govWants = tui::governanceWants(nav);
        }
        return element;
    });
    root = CatchEvent(root, [&](ftxui::Event event) {
        if (event == ftxui::Event::Custom)
        {
            // a worker posted a redraw; a new send outcome with a hash opens that transaction
            std::lock_guard<std::mutex> lock(model.mutex);
            if (model.send.last.seq != seenOutcome)
            {
                seenOutcome = model.send.last.seq;
                if (!model.send.last.txHash.empty() && model.send.last.openTx && !nav.help)
                {
                    nav.stack.push_back({.kind = tui::DetailKind::tx,
                        .number = -1,
                        .hash = model.send.last.txHash,
                        .resolved = true});
                    std::lock_guard<std::mutex> wantsLock(wantsMutex);
                    wants = tui::detailWants(nav);
                    refresher.refreshNow();
                }
                else if (!model.send.last.openTx)
                {
                    refresher.refreshNow();  // a vote / revoke: show the proposal's new state
                }
            }
            return false;
        }
        bool editingFilter = !nav.inDetail() && nav.page == tui::Page::log && nav.focus == 1;
        if (editingFilter && event.is_character())
        {
            return false;  // the filter box owns plain characters; Tab/Esc still reach Nav
        }
        if (nav.inDetail() && nav.stack.back().kind == tui::DetailKind::proposal &&
            nav.stack.back().resolved && !nav.help)
        {
            tui::SendContext context;
            {
                std::lock_guard<std::mutex> lock(model.mutex);
                context = sendContextOf();
            }
            {
                std::lock_guard<std::mutex> lock(model.mutex);
                auto const& gov = model.governance;
                auto id = nav.stack.back().number;
                if (auto it = gov.details.find(id); it != gov.details.end())
                {
                    context.proposalStatus = it->second.status;
                }
            }
            auto govAction = tui::govFormHandle(form, context, nav.stack.back().number, event);
            if (govAction == tui::SendAction::submit)
            {
                sender.submit(form.pending);
                return true;
            }
            if (govAction != tui::SendAction::toNav)
            {
                return true;
            }
        }
        if (!nav.inDetail() && !nav.help && nav.page == tui::Page::send)
        {
            tui::SendContext context;
            {
                std::lock_guard<std::mutex> lock(model.mutex);
                context = sendContextOf();
            }
            auto sendAction = tui::sendFormHandle(form, context, event);
            switch (sendAction)
            {
            case tui::SendAction::passToInput:
                syncSendSelector();
                return false;
            case tui::SendAction::redraw:
            case tui::SendAction::none:
                return true;
            case tui::SendAction::loadKey:
            {
                tui::SendRequest load;
                load.kind = tui::SendRequest::Kind::loadKey;
                load.keyFile = form.keyFile;
                sender.submit(std::move(load));
                return true;
            }
            case tui::SendAction::submit:
                sender.submit(form.pending);
                return true;
            case tui::SendAction::readFeatures:
            {
                tui::SendRequest read;
                read.kind = tui::SendRequest::Kind::readFeatures;
                sender.submit(std::move(read));
                return true;
            }
            case tui::SendAction::toNav:
                break;
            }
        }
        auto depthBefore = nav.stack.size();
        auto pageBefore = nav.page;
        auto action = tui::navHandle(nav, event);
        if (nav.page == tui::Page::gov && pageBefore != tui::Page::gov && nav.stack.empty())
        {
            // the gov page is fetched only while shown: ask for it now, not on the next tick
            {
                std::lock_guard<std::mutex> wantsLock(wantsMutex);
                govWants = tui::governanceWants(nav);
            }
            refresher.refreshNow();
        }
        if (nav.stack.size() > depthBefore)
        {
            // a detail page opened: resolve now and fetch its data without waiting for the tick
            {
                std::lock_guard<std::mutex> lock(model.mutex);
                tui::resolveDetailRefs(nav, model.explorer);
                tui::resolveProposalRefs(nav, model.governance);
            }
            {
                std::lock_guard<std::mutex> wantsLock(wantsMutex);
                wants = tui::detailWants(nav);
                govWants = tui::governanceWants(nav);
            }
            refresher.refreshNow();
        }
        switch (action)
        {
        case tui::NavAction::quit:
            // stop every worker before Exit(): ScreenInteractive drops its event sender inside
            // Exit, and a redraw posted after that from another thread is a use-after-free. The
            // refresher and the sender check the stop flag before every RPC, so the wait is at
            // most the one call in flight (the request timeout).
            stopWorkers();
            screen.Exit();
            return true;
        case tui::NavAction::refresh:
            refresher.refreshNow();
            return true;
        case tui::NavAction::redraw:
            return true;
        case tui::NavAction::none:
            // the root is Renderer(filterInput, …): an unhandled key would land in the Input
            // whatever the focus, so only the log page's filter focus lets keys through
            return !editingFilter;
        }
        return false;
    });
    // Ctrl-C goes through Nav like q so the workers stop before Exit(); FTXUI's default would
    // call Exit() itself regardless of the handler
    screen.ForceHandleCtrlC(false);

    refresher.start();
    sender.start();
    screen.Loop(root);
    stopWorkers();  // no-op after a q exit; covers Loop ending for any other reason
    return c_exitOk;
}
}  // namespace

void registerTuiCommand()
{
    registerCommand("tui",
        Command{"browser-style panel: recent blocks and txs, consensus, send, log tail, node, gov",
            "[--node-dir <dir> | --rpc <host:port>] [--key <pem|hex file>] [--version-check] "
            "[--mode none|el|opstack-el] [--web3 host:port] "
            "(auth chains add page 6 gov: committee, proposals, vote; EL modes turn page 2 into "
            "the txpool and the send page into raw only) "
            "[--snapshot-dir <dir> [--filter <text>]]\n\n"
            "PBFT state is read only in mode none (getConsensusStatus makes el / opstack-el "
            "nodes exit): with a bare --rpc pass --mode none to see view / leader / quorum. "
            "`attach getConsensusStatus` is not gated; a v1 node on the [debug] block driver is "
            "not recognised as non-PBFT",
            {"version-check"}, runTui,
            {"account", "key", "filter", "snapshot-dir", "stall-factor", "max-lag",
                "viewchange-window-ms"}});
}
}  // namespace bcos::ops
