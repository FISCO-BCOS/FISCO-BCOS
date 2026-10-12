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
#include "bcos-ops/OpsError.h"
#include "bcos-ops/cmd/StatusCmd.h"
#include "bcos-ops/collect/RpcCollector.h"
#include "bcos-ops/tui/Explorer.h"
#include "bcos-ops/tui/LogTail.h"
#include "bcos-ops/tui/Model.h"
#include "bcos-ops/tui/Nav.h"
#include "bcos-ops/tui/Refresher.h"
#include "bcos-ops/tui/views/Pages.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <ftxui/component/component.hpp>
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
    tui::Refresher refresher(
        _connection, tuiFallbacks(_connection), thresholdsFrom(_args), model, nullptr, 100000);
    refresher.setRowCapacity([]() { return tui::explorerCapacity(40); });
    refresher.setVisibleTxRange([]() { return std::make_pair(size_t(0), size_t(40)); });
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
        std::function<void(tui::Nav&)> setup;
    };
    std::vector<Shot> shots = {
        {"home", 80, 24, [](tui::Nav&) {}},
        {"home-wide", 120, 40, [](tui::Nav&) {}},
        {"consensus", 80, 24, [](tui::Nav& nav) { nav.page = tui::Page::consensus; }},
        {"node", 80, 24, [](tui::Nav& nav) { nav.page = tui::Page::node; }},
        {"log", 80, 24, [](tui::Nav& nav) { nav.page = tui::Page::log; }},
        {"help", 80, 24, [](tui::Nav& nav) { nav.help = true; }},
    };
    int index = 1;
    for (auto const& shot : shots)
    {
        tui::Nav nav;
        shot.setup(nav);
        auto screen = ftxui::Screen::Create(
            ftxui::Dimension::Fixed(shot.width), ftxui::Dimension::Fixed(shot.height));
        {
            std::lock_guard<std::mutex> lock(model.mutex);
            tui::Frame frame{shot.width, shot.height, tuiNowMs(), nullptr};
            ftxui::Render(screen, tui::renderFrame(model, nav, frame, filter));
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
    tui::Nav nav;
    auto screen = ScreenInteractive::Fullscreen();
    auto redraw = [&screen]() { screen.PostEvent(ftxui::Event::Custom); };
    // the refresher thread reads these; the UI thread writes them while rendering
    std::atomic<int> terminalRows{24};
    std::atomic<size_t> visibleTxFrom{0};
    std::atomic<size_t> visibleTxCount{20};
    tui::Refresher refresher(
        connection, tuiFallbacks(connection), thresholdsFrom(_args), model, redraw, 2000);
    refresher.setRowCapacity([&]() { return tui::explorerCapacity(terminalRows.load()); });
    refresher.setVisibleTxRange(
        [&]() { return std::make_pair(visibleTxFrom.load(), visibleTxCount.load()); });

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
        stopTail = true;
        if (tailThread.joinable())
        {
            tailThread.join();
        }
    };

    std::string filter;
    auto filterInput = Input(&filter, "event name or channel (e.g. ViewChange, TXPOOL)");
    auto root = Renderer(filterInput, [&]() {
        std::lock_guard<std::mutex> lock(model.mutex);
        terminalRows = screen.dimy();
        bool editingFilter = !nav.inDetail() && nav.page == tui::Page::log && nav.focus == 1;
        tui::Frame frame{screen.dimx(), screen.dimy(), tuiNowMs(),
            editingFilter ? filterInput->Render() : nullptr};
        auto element = tui::renderFrame(model, nav, frame, filter);
        visibleTxFrom = nav.txs.firstVisible();
        visibleTxCount = nav.txs.pageRows;
        return element;
    });
    root = CatchEvent(root, [&](ftxui::Event event) {
        bool editingFilter = !nav.inDetail() && nav.page == tui::Page::log && nav.focus == 1;
        if (editingFilter && event.is_character())
        {
            return false;  // the filter box owns plain characters; Tab/Esc still reach Nav
        }
        switch (tui::navHandle(nav, event))
        {
        case tui::NavAction::quit:
            // stop every worker before Exit(): ScreenInteractive drops its event sender inside
            // Exit, and a redraw posted after that from another thread is a use-after-free. The
            // refresher stop may wait for one in-flight RPC (at most the request timeout).
            stopWorkers();
            screen.Exit();
            return true;
        case tui::NavAction::refresh:
            refresher.refreshNow();
            return true;
        case tui::NavAction::redraw:
            return true;
        case tui::NavAction::none:
            return false;
        }
        return false;
    });

    refresher.start();
    screen.Loop(root);
    stopWorkers();  // no-op after a q exit; covers Loop ending for any other reason
    return c_exitOk;
}
}  // namespace

void registerTuiCommand()
{
    registerCommand("tui",
        Command{"browser-style panel: recent blocks and txs, consensus, send, log tail, node",
            "[--node-dir <dir> | --rpc <host:port>] [--version-check] "
            "[--snapshot-dir <dir> [--filter <text>]]",
            {"version-check"}, runTui});
}
}  // namespace bcos::ops
