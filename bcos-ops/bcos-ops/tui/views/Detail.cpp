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
 * @brief the detail stack: a block (header fields + its transactions) and a transaction (fields
 *        + receipt, revert reason decoded, input decoded for txs this session sent). The refs
 *        Nav pushes are row indexes; resolveDetailRefs pins them to heights and hashes first.
 * @file Detail.cpp
 */
#include "ListView.h"
#include "Pages.h"
#include "bcos-ops/Output.h"
#include "bcos-ops/tx/Abi.h"
#include "bcos-ops/tx/PresetCall.h"
#include "bcos-ops/tx/Presets.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <algorithm>

namespace bcos::ops::tui
{
using namespace ftxui;

namespace
{
std::string detailText(Json::Value const& _value)
{
    if (_value.isNull())
    {
        return "-";
    }
    if (_value.isString())
    {
        return _value.asString();
    }
    if (_value.isIntegral())
    {
        return std::to_string(_value.asInt64());
    }
    if (_value.isBool())
    {
        return _value.asBool() ? "true" : "false";
    }
    return _value.toStyledString();
}

/// a hex payload shown as its byte length plus the first bytes
std::string detailHexSummary(std::string const& _hex, size_t _keepChars = 40)
{
    auto body = _hex.starts_with("0x") ? _hex.substr(2) : _hex;
    if (body.empty())
    {
        return "0x (0 bytes)";
    }
    auto shown = body.size() > _keepChars ? body.substr(0, _keepChars) + "…" : body;
    return "0x" + shown + " (" + std::to_string(body.size() / 2) + " bytes)";
}

/// the node writes the parent as parentInfo[{blockNumber, blockHash}]
std::string detailParentHash(Json::Value const& _block)
{
    if (_block.isMember("parentHash"))
    {
        return _block["parentHash"].asString();
    }
    auto const& parents = _block["parentInfo"];
    if (parents.isArray() && !parents.empty())
    {
        return parents[0].get("blockHash", "-").asString();
    }
    return "-";
}

/// the node hex-encodes the nonce's decimal digits (toHex(transaction.nonce())); show the number
std::string detailNonce(Json::Value const& _nonce)
{
    if (!_nonce.isString())
    {
        return detailText(_nonce);
    }
    auto hex = _nonce.asString();
    auto body = hex.starts_with("0x") ? hex.substr(2) : hex;
    if (body.empty() || body.size() % 2 != 0 ||
        body.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
    {
        return hex;
    }
    std::string digits;
    for (size_t i = 0; i < body.size(); i += 2)
    {
        auto byte = static_cast<char>(std::stoi(body.substr(i, 2), nullptr, 16));
        if (byte < '0' || byte > '9')
        {
            return hex;  // not a decimal text: keep the node's form
        }
        digits += byte;
    }
    return digits;
}

/// the tx hash at _index of a cached block (the node returns objects or hash strings)
std::string detailTxHashIn(Json::Value const& _block, size_t _index)
{
    auto const& txs = _block["transactions"];
    if (!txs.isArray() || _index >= txs.size())
    {
        return "";
    }
    auto const& tx = txs[static_cast<Json::ArrayIndex>(_index)];
    return tx.isString() ? tx.asString() : tx.get("hash", "").asString();
}

/// the explorer tx rows of one block, in block order
std::vector<TxRow> detailRowsOf(ExplorerState const& _explorer, int64_t _number)
{
    std::vector<TxRow> rows;
    for (auto const& row : _explorer.txs)
    {
        if (row.blockNumber == _number)
        {
            rows.push_back(row);
        }
    }
    return rows;
}

/// the tx rows a block detail lists: the cached block when loaded, else the explorer rows
std::vector<TxRow> detailBlockTxs(ExplorerState const& _explorer, int64_t _number)
{
    auto cached = _explorer.blockDetails.find(_number);
    if (cached == _explorer.blockDetails.end())
    {
        return detailRowsOf(_explorer, _number);
    }
    auto rows = txRowsFromBlock(cached->second);
    for (auto& row : rows)  // statuses the home list already fetched
    {
        auto it = _explorer.txDetails.find(row.hash);
        if (it != _explorer.txDetails.end() && !it->second.pending)
        {
            row.status = it->second.receipt.get("status", -1).asInt();
            continue;
        }
        for (auto const& known : _explorer.txs)
        {
            if (known.hash == row.hash)
            {
                row.status = known.status;
                break;
            }
        }
    }
    return rows;
}

Element detailField(std::string const& _label, std::string const& _value, int _labelWidth = 12)
{
    return hbox({text(_label) | dim | size(WIDTH, EQUAL, _labelWidth), text(_value)});
}

Element renderBlockDetail(
    Model const& _model, Nav& _nav, Frame const& _frame, DetailRef const& _ref)
{
    auto const& explorer = _model.explorer;
    auto cached = explorer.blockDetails.find(_ref.number);
    Json::Value block = cached == explorer.blockDetails.end() ? Json::Value() : cached->second;
    auto row = std::find_if(explorer.blocks.begin(), explorer.blocks.end(),
        [&](BlockRow const& r) { return r.number == _ref.number; });
    auto field = [&](char const* key) { return detailText(block[key]); };
    std::string hash = block.isMember("hash")       ? block["hash"].asString() :
                       row != explorer.blocks.end() ? row->hash :
                                                      _ref.hash;
    int64_t timestamp = block.isMember("timestamp")  ? block["timestamp"].asInt64() :
                        row != explorer.blocks.end() ? row->timestampMs :
                                                       0;
    std::string sealer = block.isMember("sealer")     ? field("sealer") :
                         row != explorer.blocks.end() ? row->sealer :
                                                        "-";
    auto signatures = block["signatureList"].isArray() ? block["signatureList"].size() : 0U;
    Elements left = {detailField("number", std::to_string(_ref.number)),
        detailField("hash", hash, 6),
        detailField("parentHash", block.isNull() ? "-" : detailParentHash(block)),
        detailField("timestamp",
            clockText(timestamp) + "  " +
                (timestamp > 0 ? ageText(std::max<int64_t>(0, _frame.nowMs - timestamp)) + " ago" :
                                 "-")),
        detailField("sealer", sealer)};
    Elements right = {detailField("gasUsed", field("gasUsed")),
        detailField("txsRoot", abridged(field("txsRoot"), 8)),
        detailField("receiptsRoot", abridged(field("receiptsRoot"), 8)),
        detailField("stateRoot", abridged(field("stateRoot"), 8)),
        detailField("signatures", block.isNull() ? "loading…" : std::to_string(signatures))};
    Element header = _frame.width >= 100 ? hbox({vbox(left) | flex, vbox(right) | flex}) :
                                           vbox({vbox(left), vbox(right)});
    if (block.isNull())
    {
        header = vbox({header, text("loading block detail…") | dim});
    }

    auto rows = detailBlockTxs(explorer, _ref.number);
    std::deque<TxRow> list(rows.begin(), rows.end());
    int listHeight =
        std::max(4, _frame.height - static_cast<int>(left.size()) -
                        (_frame.width >= 100 ? 0 : static_cast<int>(right.size())) - 3);
    std::vector<ListColumn> columns = {
        {"hash", 11, 0}, {"from", 9, 3}, {"to", 9, 4}, {"status", 6, 1}};
    auto fit = fitListColumns(columns, _frame.width - 2);
    auto txs = renderListView<TxRow>(
        " transactions ", fit, list, _nav.detailTxs, true, listHeight,
        [&](TxRow const& r, ListColumn const& column) -> std::string {
            if (column.name == "hash")
            {
                return abridged(r.hash, 4);
            }
            if (column.name == "from")
            {
                return r.from.empty() ? "-" : abridged(r.from, 4);
            }
            if (column.name == "to")
            {
                return r.to.empty() ? "create" : abridged(r.to, 4);
            }
            return txStatusWord(r.status);
        },
        [](TxRow const&) { return false; });
    if (rows.empty())
    {
        _nav.detailTxs.count = 0;
    }
    return window(text(" block " + std::to_string(_ref.number) + " "),
        vbox({header, separatorLight(), txs | flex}), LIGHT);
}

Element renderTxDetail(Model const& _model, Frame const& _frame, DetailRef const& _ref)
{
    auto const& explorer = _model.explorer;
    auto cached = explorer.txDetails.find(_ref.hash);
    bool loaded = cached != explorer.txDetails.end();
    Json::Value tx = loaded ? cached->second.transaction : Json::Value();
    Json::Value receipt =
        loaded && !cached->second.pending ? cached->second.receipt : Json::Value();
    auto field = [&](Json::Value const& object, char const* key) {
        return detailText(object[key]);
    };
    std::string input = tx.get("input", "").asString();
    Elements left = {detailField("hash", _ref.hash, 6), detailField("from", field(tx, "from")),
        detailField("to", tx.get("to", "").asString().empty() ? "create" : field(tx, "to")),
        detailField("nonce", tx.isMember("nonce") ? detailNonce(tx["nonce"]) : "-"),
        detailField("blockNumber", receipt.isMember("blockNumber") ? field(receipt, "blockNumber") :
                                                                     field(tx, "blockNumber")),
        detailField("blockLimit", field(tx, "blockLimit")),
        detailField("input", tx.isMember("input") ? detailHexSummary(input) : "-"),
        detailField("abi",
            tx.isMember("abi") ? std::to_string(tx["abi"].asString().size()) + " chars" : "-")};
    auto preset = _model.sentPresets.find(_ref.hash);
    if (preset != _model.sentPresets.end() && preset->second != nullptr && !input.empty())
    {
        std::string decoded;
        try
        {
            decoded = Abi(_model.status.smCrypto.value_or(false))
                          .decodeInputBySignature(std::string(preset->second->signature),
                              fromHex(input.starts_with("0x") ? input.substr(2) : input));
        }
        catch (std::exception const&)
        {
            decoded = "(not decodable)";
        }
        left.push_back(detailField("method", std::string(preset->second->signature)));
        left.push_back(detailField("params", sanitizeForTerminal(decoded)));
    }

    Elements right;
    if (!loaded)
    {
        right.push_back(text("loading transaction…") | dim);
    }
    else if (cached->second.pending)
    {
        right.push_back(detailField("status", "pending"));
        right.push_back(text("receipt not written yet; refreshing") | dim);
    }
    else
    {
        int status = receipt.get("status", -1).asInt();
        auto statusText = std::to_string(status) + " " + txStatusWord(status);
        right.push_back(detailField("status", statusText) |
                        (status == 0 ? color(Color::Green) : color(Color::Red)));
        right.push_back(detailField("gasUsed", field(receipt, "gasUsed")));
        auto contract = receipt.get("contractAddress", "").asString();
        right.push_back(detailField("contract", contract.empty() ? "-" : contract));
        auto output = receipt.get("output", "").asString();
        right.push_back(detailField("output", detailHexSummary(output)));
        auto message = receipt.get("message", "").asString();
        auto reason = detailRevertReason(output);
        if (!reason.empty())
        {
            right.push_back(detailField("revert", sanitizeForTerminal(reason)) | color(Color::Red));
        }
        if (!message.empty())
        {
            auto line = detailField("message", sanitizeForTerminal(message));
            right.push_back(status != 0 ? line | color(Color::Red) : line);
        }
        auto logs = receipt["logEntries"].isArray() ? receipt["logEntries"].size() : 0U;
        right.push_back(detailField("logs", std::to_string(logs)));
    }
    Element body = _frame.width >= 100 ?
                       hbox({vbox(left) | flex, separatorLight(), vbox(right) | flex}) :
                       vbox({vbox(left), separatorLight(), vbox(right)});
    return window(text(" tx " + abridged(_ref.hash, 6) + " "), body, LIGHT);
}
}  // namespace

std::string detailRevertReason(std::string const& _outputHex)
{
    return revertReason(_outputHex, false);  // Error(string) decoding does not hash
}

void resolveDetailRefs(Nav& _nav, ExplorerState const& _explorer)
{
    for (size_t i = 0; i < _nav.stack.size(); ++i)
    {
        auto& ref = _nav.stack[i];
        if (ref.resolved || ref.kind == DetailKind::proposal)
        {
            continue;
        }
        auto index = static_cast<size_t>(std::max<int64_t>(0, ref.number));
        if (ref.kind == DetailKind::block)
        {
            if (index < _explorer.blocks.size())
            {
                ref.number = _explorer.blocks[index].number;
                ref.hash = _explorer.blocks[index].hash;
                ref.resolved = true;
            }
            continue;
        }
        if (!ref.hash.empty())
        {
            ref.resolved = true;  // pushed with a hash (a sent tx): nothing to map
            continue;
        }
        if (i > 0 && _nav.stack[i - 1].kind == DetailKind::block && _nav.stack[i - 1].resolved)
        {
            auto parent = _nav.stack[i - 1].number;
            auto cached = _explorer.blockDetails.find(parent);
            std::string hash =
                cached == _explorer.blockDetails.end() ? "" : detailTxHashIn(cached->second, index);
            if (hash.empty())
            {
                auto rows = detailRowsOf(_explorer, parent);
                hash = index < rows.size() ? rows[index].hash : "";
            }
            if (!hash.empty())
            {
                ref.hash = hash;
                ref.number = parent;
                ref.resolved = true;
            }
            continue;
        }
        if (index < _explorer.txs.size())
        {
            ref.hash = _explorer.txs[index].hash;
            ref.number = _explorer.txs[index].blockNumber;
            ref.resolved = true;
        }
    }
}

std::vector<DetailWant> detailWants(Nav const& _nav)
{
    std::vector<DetailWant> wants;
    for (auto const& ref : _nav.stack)
    {
        if (!ref.resolved)
        {
            continue;
        }
        if (ref.kind == DetailKind::block)
        {
            wants.push_back({true, ref.number, ref.hash});
        }
        else if (ref.kind == DetailKind::tx)
        {
            wants.push_back({false, -1, ref.hash});
        }
    }
    return wants;
}

Element renderDetail(Model const& _model, Nav& _nav, Frame const& _frame)
{
    auto const& ref = _nav.stack.back();
    if (ref.kind == DetailKind::proposal)
    {
        _nav.detailTxs.count = 0;
        return window(text(" proposal " + std::to_string(ref.number) + " "),
            text("proposal detail arrives with ticket 16") | dim, LIGHT);
    }
    if (!ref.resolved)
    {
        _nav.detailTxs.count = 0;
        return window(text(ref.kind == DetailKind::block ? " block " : " tx "),
            text(ref.kind == DetailKind::block ? "row no longer loaded; Esc" : "loading…") | dim,
            LIGHT);
    }
    if (ref.kind == DetailKind::block)
    {
        return renderBlockDetail(_model, _nav, _frame, ref);
    }
    _nav.detailTxs.count = 0;
    return renderTxDetail(_model, _frame, ref);
}
}  // namespace bcos::ops::tui
