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
 * @brief page 2 in the EL modes: the pool's pending / queued rows (opstack-el with web3), or
 *        the pending count with the reason the content is not shown
 * @file Txpool.cpp
 */
#include "Pages.h"
#include "bcos-ops/Output.h"
#include <ftxui/dom/table.hpp>

namespace bcos::ops::tui
{
using namespace ftxui;

Element renderTxpool(Model const& _model)
{
    auto const& pool = _model.txpool;
    auto const& s = _model.status;
    auto pending = s.pendingTxSize ? std::to_string(*s.pendingTxSize) : "-";
    if (!pool.content)
    {
        Elements lines = {hbox({text(" pending ") | dim, text(pending)})};
        if (!pool.error.empty())
        {
            lines.push_back(text(" " + sanitizeForTerminal(pool.error)) | color(Color::Yellow));
        }
        auto title = pool.note.empty() ? std::string(" txpool ") : " txpool: " + pool.note + " ";
        return window(text(sanitizeForTerminal(title)), vbox(std::move(lines)), LIGHT);
    }
    size_t pendingRows = 0;
    std::vector<std::vector<std::string>> rows = {
        {"state", "hash", "from", "nonce", "to", "value"}};
    for (auto const& row : pool.rows)
    {
        pendingRows += row.pending ? 1 : 0;
        rows.push_back({row.pending ? "pending" : "queued", abridged(row.hash, 4),
            abridged(row.from, 4), row.nonce >= 0 ? std::to_string(row.nonce) : "-",
            row.to.empty() ? "create" : abridged(row.to, 4), row.value.empty() ? "-" : row.value});
    }
    for (auto& row : rows)
    {
        for (auto& cell : row)
        {
            cell = sanitizeForTerminal(cell) + "  ";
        }
    }
    Table table(rows);
    table.SelectRow(0).Decorate(dim);
    auto head = hbox({text(" pending ") | dim, text(std::to_string(pendingRows)),
        text("  queued ") | dim, text(std::to_string(pool.rows.size() - pendingRows)),
        text("  getPendingTxSize ") | dim, text(pending)});
    Elements body = {head, separatorLight()};
    body.push_back(pool.rows.empty() ? text(" the pool is empty") | dim : table.Render());
    if (!pool.error.empty())
    {
        body.push_back(text(" " + sanitizeForTerminal(pool.error)) | color(Color::Yellow));
    }
    return window(text(" txpool "), vbox(std::move(body)), LIGHT);
}
}  // namespace bcos::ops::tui
