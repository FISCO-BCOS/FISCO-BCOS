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
 * @brief `fisco-bcos tui`: five views, 2 s refresh, 1 s log tail, keys 1-5 / r / q
 * @file TuiCmd.cpp
 */
#include "bcos-ops/Args.h"
#include "bcos-ops/Cli.h"
#include "bcos-ops/Connect.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/cmd/StatusCmd.h"
#include "bcos-ops/collect/GroupFacts.h"
#include "bcos-ops/collect/RpcCollector.h"
#include "bcos-ops/tui/LogTail.h"
#include "bcos-ops/tui/Model.h"
#include "bcos-ops/tui/Refresher.h"
#include "bcos-ops/tui/views/Views.h"
#include "bcos-ops/tx/Account.h"
#include "bcos-ops/tx/Smoke.h"
#include "bcos-ops/tx/TxSender.h"
#include <atomic>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ostream>
#include <thread>

namespace bcos::ops
{
namespace
{
using namespace ftxui;  // bcos::ops::Event clashes with ftxui::Event, which is always qualified

int runTui(Args const& _args, std::ostream& _out, std::ostream& _err)
{
    (void)_err;
    if (_args.flag("version-check"))
    {
        // CI smoke: the TUI library links and the command dispatches, without entering the terminal
        _out << "ftxui linked; tui available\n";
        return c_exitOk;
    }
    auto options = connectOptionsFrom(_args);
    auto connection = connect(options);
    LocalFallbacks fallbacks;
    if (connection.nodeDir)
    {
        fallbacks.txpoolLimit = static_cast<int64_t>(connection.nodeDir->txpoolLimit);
        fallbacks.consensusTimeoutMs = connection.nodeDir->consensusTimeoutMs;
    }
    tui::Model model;
    auto screen = ScreenInteractive::Fullscreen();
    auto redraw = [&screen]() { screen.PostEvent(ftxui::Event::Custom); };
    tui::Refresher refresher(connection, fallbacks, thresholdsFrom(_args), model, redraw, 2000);

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
                    std::lock_guard<std::mutex> lock(model.mutex);
                    for (auto& event : events)
                    {
                        model.logTail.push_back(std::move(event));
                    }
                    while (model.logTail.size() > 500)
                    {
                        model.logTail.pop_front();
                    }
                    redraw();
                }
                for (int i = 0; i < 10 && !stopTail; ++i)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        });
    }

    // smoke on a worker thread; the button is disabled while it runs
    std::thread smokeThread;
    auto startSmoke = [&]() {
        {
            std::lock_guard<std::mutex> lock(model.mutex);
            if (model.smokeRunning)
            {
                return;
            }
            model.smokeRunning = true;
            model.smokeError.clear();
            model.smoke.reset();
        }
        if (smokeThread.joinable())
        {
            smokeThread.join();
        }
        smokeThread = std::thread([&]() {
            try
            {
                auto facts = groupFacts(connection.call, connection.group, std::nullopt);
                bool sm = facts.smCrypto.value_or(false);
                TxSender sender(connection.call, connection.group, facts.chainId, sm,
                    makeAccount(sm, _args.option("account")));
                auto result = runSmoke(sender, facts.smCrypto.value_or(false));
                std::lock_guard<std::mutex> lock(model.mutex);
                model.smoke = std::move(result);
            }
            catch (std::exception const& e)
            {
                std::lock_guard<std::mutex> lock(model.mutex);
                model.smokeError = e.what();
            }
            {
                std::lock_guard<std::mutex> lock(model.mutex);
                model.smokeRunning = false;
            }
            redraw();
        });
    };

    auto stopWorkers = [&]() {
        refresher.stop();
        stopTail = true;
        if (tailThread.joinable())
        {
            tailThread.join();
        }
        if (smokeThread.joinable())
        {
            smokeThread.join();
        }
    };

    int selected = 0;
    std::string filter;
    auto overview = Renderer([&model]() {
        std::lock_guard<std::mutex> lock(model.mutex);
        return tui::renderOverview(model);
    });
    auto consensus = Renderer([&model]() {
        std::lock_guard<std::mutex> lock(model.mutex);
        return tui::renderConsensus(model);
    });
    auto sync = Renderer([&model]() {
        std::lock_guard<std::mutex> lock(model.mutex);
        return tui::renderSync(model);
    });
    auto txView = tui::makeTxView(model, startSmoke);
    auto logView = tui::makeLogView(model, filter);
    auto tabs = Container::Tab({overview, consensus, sync, txView, logView}, &selected);

    auto root = Renderer(tabs, [&]() {
        Element top;
        {
            std::lock_guard<std::mutex> lock(model.mutex);
            top = tui::renderTopBar(model);
        }
        auto footer =
            hbox({text(" 1 overview  2 consensus  3 sync  4 tx  5 log  r refresh  q quit ") | dim});
        return vbox({top, tabs->Render() | flex, footer});
    });
    root = CatchEvent(root, [&](ftxui::Event event) {
        // the log view's input box owns plain characters while selected; keep the hotkeys on
        // the other views and always honour Escape/q from anywhere but the input
        if (selected == 4 && event.is_character() && event.character() != "\x1b")
        {
            return false;
        }
        if (event == ftxui::Event::Character('q') || event == ftxui::Event::Escape)
        {
            // stop every worker before Exit(): ScreenInteractive drops its event sender inside
            // Exit, and a redraw posted after that from another thread is a use-after-free. The
            // refresher stop may wait for one in-flight RPC (at most the request timeout).
            stopWorkers();
            screen.Exit();
            return true;
        }
        if (event == ftxui::Event::Character('r'))
        {
            refresher.refreshNow();
            return true;
        }
        if (event.is_character() && event.character().size() == 1 && event.character()[0] >= '1' &&
            event.character()[0] <= '5')
        {
            selected = event.character()[0] - '1';
            return true;
        }
        return false;
    });

    refresher.start();
    screen.Loop(root);
    stopWorkers();  // no-op after a q/Escape exit; covers Loop ending for any other reason
    return c_exitOk;
}
}  // namespace

void registerTuiCommand()
{
    registerCommand(
        "tui", Command{"interactive panel: overview, consensus, sync/p2p, tx smoke, log tail",
                   "[--node-dir <dir> | --rpc <host:port>] [--account <pem>] [--version-check]",
                   {"version-check"}, runTui});
}
}  // namespace bcos::ops
