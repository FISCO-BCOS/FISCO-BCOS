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
 * @brief `fisco-bcos tx smoke|deploy|call|send|get`
 * @file TxCmd.cpp
 */
#include "TxCmd.h"
#include "TxContext.h"
#include "bcos-ops/Args.h"
#include "bcos-ops/Cli.h"
#include "bcos-ops/Connect.h"
#include "bcos-ops/HttpRpc.h"
#include "bcos-ops/Mode.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/Output.h"
#include "bcos-ops/RpcClient.h"
#include "bcos-ops/tx/Abi.h"
#include "bcos-ops/tx/PresetCall.h"
#include "bcos-ops/tx/Smoke.h"
#include "bcos-ops/tx/TxSender.h"
#include <bcos-framework/executor/PrecompiledTypeDef.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <sstream>

namespace bcos::ops
{
namespace
{
int runTxDispatch(Connection const& _connection, Args const& _args, std::ostream& _out);

std::string readFile(std::string const& _path, std::string_view _what)
{
    std::ifstream in(_path);
    if (!in)
    {
        throw OpsError(c_exitUsage, std::string(_what) + " not readable: " + _path);
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string trimmed(std::string _text)
{
    while (!_text.empty() && std::isspace(static_cast<unsigned char>(_text.back())) != 0)
    {
        _text.pop_back();
    }
    size_t start = 0;
    while (start < _text.size() && std::isspace(static_cast<unsigned char>(_text[start])) != 0)
    {
        ++start;
    }
    return _text.substr(start);
}

/// a positional that is a readable file is read, `@path` forces the file form
std::string hexOrFile(std::string const& _value, std::string_view _what)
{
    if (_value.starts_with("@"))
    {
        return trimmed(readFile(_value.substr(1), _what));
    }
    if (std::ifstream(_value).good())
    {
        return trimmed(readFile(_value, _what));
    }
    return trimmed(_value);
}

SendOptions sendOptionsFrom(Args const& _args)
{
    SendOptions options;
    options.value = _args.optionOr("value", "");
    options.nonce = _args.optionOr("nonce", "");
    if (_args.option("block-limit"))
    {
        options.blockLimit = _args.numberOr<int64_t>("block-limit", 0);
    }
    return options;
}

}  // namespace

int printRpcRefusal(std::ostream& _out, OpsRpcError const& _error, bool _json)
{
    if (_json)
    {
        Json::Value value;
        value["error"]["code"] = _error.code;
        value["error"]["message"] = _error.message;
        value["error"]["method"] = _error.method;
        printJson(_out, value);
    }
    else
    {
        _out << "rejected: code=" << _error.code << " msg=" << _error.message << '\n';
    }
    return c_exitChecksFailed;
}

namespace
{
/// a failed receipt carries no message on 3.18 (the revert text sits in the output as
/// Error(string)); fill it in so --json consumers can read .message
Receipt withRevertMessage(Receipt _receipt)
{
    if (!_receipt.ok() && _receipt.message.empty())
    {
        _receipt.message = revertReason(_receipt.output, false);
        if (!_receipt.message.empty())
        {
            _receipt.raw["message"] = _receipt.message;
        }
    }
    return _receipt;
}

void printReceipt(std::ostream& _out, Receipt const& _given, bool _json)
{
    auto _receipt = withRevertMessage(_given);
    if (_json)
    {
        printJson(_out, _receipt.raw);
        return;
    }
    std::vector<std::pair<std::string, std::string>> rows;
    rows.emplace_back("txHash", _receipt.txHash);
    rows.emplace_back(
        "status", std::to_string(_receipt.status) + (_receipt.ok() ? " ok" : " FAILED"));
    if (!_receipt.contractAddress.empty())
    {
        rows.emplace_back("contract", _receipt.contractAddress);
    }
    rows.emplace_back("block", std::to_string(_receipt.blockNumber));
    rows.emplace_back("gasUsed", _receipt.gasUsed);
    if (!_receipt.message.empty())
    {
        rows.emplace_back("message", _receipt.message);
    }
    printRows(_out, rows);
}

int runSmokeCmd(Connection const& _connection, Args const& _args, std::ostream& _out)
{
    auto facts = chainFacts(_connection);
    auto sender = makeSender(_connection, facts, _args);
    auto result = runSmoke(*sender, facts.sm);
    if (wantJson(_args.flag("json")))
    {
        auto json = result.toJson();
        json["account"] = sender->address();
        printJson(_out, json);
    }
    else
    {
        for (auto const& step : result.steps)
        {
            _out << std::left << std::setw(8) << step.name << std::setw(16)
                 << (step.txHash.empty() ? "-" : abridged(step.txHash, 4))
                 << (step.ok ? "ok   " : "FAIL ") << sanitizeForTerminal(step.detail) << '\n';
        }
        if (!result.ok)
        {
            _out << "smoke failed: reason=" << sanitizeForTerminal(result.reason) << '\n';
        }
    }
    return result.exitCode();
}

/// `/apps/<name>/<version>` → (name, version); throws on any other shape
std::pair<std::string, std::string> splitLinkPath(std::string const& _path)
{
    constexpr std::string_view c_prefix = "/apps/";
    auto slash = _path.find('/', c_prefix.size());
    if (!_path.starts_with(c_prefix) || slash == std::string::npos || slash == c_prefix.size() ||
        slash + 1 >= _path.size() || _path.find('/', slash + 1) != std::string::npos)
    {
        throw OpsError(c_exitUsage, "--link expects /apps/<name>/<version>, got " + _path);
    }
    return {_path.substr(c_prefix.size(), slash - c_prefix.size()), _path.substr(slash + 1)};
}

/// BFS `link(name, version, address, abi)` at 0x…100e (BFSPrecompiled.cpp linkAdaptCNS, the
/// form console's `deploy -l` uses): registers the contract as /apps/<name>/<version> so
/// `readlink` resolves it. The absolute-path overload `link(string,string,string)` builds its
/// table name with getContractTableName("/apps/", path) and therefore never lands where
/// readlink looks (verified on a 3.16 chain: code 0, readlink 0x0). Returns the int32 code.
int32_t linkContract(TxSender& _sender, bool _sm, std::string const& _path,
    std::string const& _address, std::string const& _abiText)
{
    auto [name, version] = splitLinkPath(_path);
    Abi abi(_sm);
    Json::Value params(Json::arrayValue);
    params.append(name);
    params.append(version);
    params.append(_address);
    params.append(_abiText);
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    auto data = abi.encodeMethodBySignature(
        "link(string,string,string,string)", Json::writeString(writer, params));
    auto receipt =
        _sender.send("0x" + std::string(bcos::precompiled::BFS_ADDRESS), std::move(data), "");
    if (!receipt.ok())
    {
        throw OpsError(c_exitChecksFailed, "link " + _path + " failed: receipt status " +
                                               std::to_string(receipt.status) + " " +
                                               receipt.message);
    }
    auto outputHex = receipt.output.starts_with("0x") ? receipt.output.substr(2) : receipt.output;
    if (outputHex.size() < 64)
    {
        throw OpsError(
            c_exitChecksFailed, "link " + _path + " returned no code: " + receipt.output);
    }
    return static_cast<int32_t>(std::stoul(outputHex.substr(56, 8), nullptr, 16));
}

int runDeploy(Connection const& _connection, Args const& _args, std::ostream& _out)
{
    auto const& positionals = _args.positionals();
    if (positionals.empty())
    {
        throw OpsError(c_exitUsage,
            "usage: tx deploy <bin-file|hex> --abi <abi-file> [args...] [--value N] "
            "[--link /apps/<name>/<version>]");
    }
    auto abiPath = _args.option("abi");
    if (!abiPath)
    {
        throw OpsError(c_exitUsage, "tx deploy needs --abi <abi-file>");
    }
    std::string bin = hexOrFile(positionals[0], "bin file");
    if (bin.starts_with("0x"))
    {
        bin = bin.substr(2);
    }
    auto abiText = readFile(*abiPath, "abi file");
    auto link = _args.option("link");
    if (link)
    {
        (void)splitLinkPath(*link);  // reject a bad path before anything is sent
    }
    auto facts = chainFacts(_connection);
    auto sender = makeSender(_connection, facts, _args);
    Abi abi(facts.sm);
    std::vector<std::string> ctorArgs(positionals.begin() + 1, positionals.end());
    auto data = abi.encodeConstructor(abiText, bin, argsToJsonArray(ctorArgs));
    bool json = wantJson(_args.flag("json"));
    auto receipt = sender->send("", std::move(data), abiText, sendOptionsFrom(_args));
    // the contract is on chain once the deploy receipt is ok: a failing link must not hide
    // the address, so every link outcome lands in the receipt and the receipt is printed
    bool linked = false;
    std::string linkFailure;
    if (receipt.ok() && link)
    {
        receipt.raw["link"] = *link;
        try
        {
            auto code = linkContract(*sender, facts.sm, *link, receipt.contractAddress, abiText);
            receipt.raw["linkCode"] = code;
            linked = code == 0;
            if (!linked)
            {
                receipt.raw["linkCodeName"] = precompiledCodeName(code);
                linkFailure = "code " + std::to_string(code) + " " + precompiledCodeName(code);
            }
        }
        catch (OpsRpcError const& e)
        {
            receipt.raw["linkError"] =
                e.method + " code=" + std::to_string(e.code) + " " + e.message;
            linkFailure = receipt.raw["linkError"].asString();
        }
        catch (OpsError const& e)
        {
            receipt.raw["linkError"] = e.what();
            linkFailure = e.what();
        }
    }
    printReceipt(_out, receipt, json);
    if (!json && link)
    {
        printRows(_out, {{"link", !receipt.ok() ? *link + " SKIPPED (deploy failed)" :
                                  linked        ? *link :
                                                  *link + " FAILED " + linkFailure}});
    }
    return receipt.ok() && (!link || linked) ? c_exitOk : c_exitChecksFailed;
}

int runRaw(Connection const& _connection, Args const& _args, std::ostream& _out)
{
    if (_args.positionals().empty())
    {
        throw OpsError(c_exitUsage, "usage: tx raw <signed-hex|@file>");
    }
    auto hex = normalizeRawTxHex(hexOrFile(_args.positionals()[0], "raw transaction file"));
    if (isElMode(_connection.mode))
    {
        // EL modes: an Ethereum RLP transaction to the web3 listener, then poll its receipt
        if (!_connection.web3Endpoint)
        {
            throw OpsError(c_exitUsage, "mode " + _connection.mode +
                                            ": tx raw goes to eth_sendRawTransaction; enable "
                                            "[web3_rpc] or pass --web3 host:port");
        }
        auto web3 = makeHttpRpcCall(*_connection.web3Endpoint, c_defaultRequestTimeoutMs);
        auto waitMs = static_cast<int>(_args.numberOr<double>("wait", 60) * 1000);
        auto receipt = sendRawEth(web3, hex, waitMs);
        printReceipt(_out, receipt, wantJson(_args.flag("json")));
        return receipt.ok() ? c_exitOk : c_exitChecksFailed;
    }
    auto facts = chainFacts(_connection);
    auto sender = makeSender(_connection, facts, _args);
    bool json = wantJson(_args.flag("json"));
    auto receipt = sender->sendRaw(hex);
    printReceipt(_out, receipt, json);
    return receipt.ok() ? c_exitOk : c_exitChecksFailed;
}

enum class TxMode
{
    Call,
    Send,
    Sign
};

int runCallOrSend(
    Connection const& _connection, Args const& _args, std::ostream& _out, TxMode _mode)
{
    auto const& positionals = _args.positionals();
    bool send = _mode != TxMode::Call;
    if (positionals.size() < 2)
    {
        throw OpsError(c_exitUsage, std::string("usage: tx ") +
                                        (_mode == TxMode::Call    ? "call" :
                                            _mode == TxMode::Send ? "send" :
                                                                    "sign") +
                                        " <address> --abi <abi-file> <function> [args...]" +
                                        (send ? " [--value N] [--nonce S] [--block-limit N]" : ""));
    }
    auto abiPath = _args.option("abi");
    if (!abiPath)
    {
        throw OpsError(c_exitUsage, "needs --abi <abi-file>");
    }
    auto abiText = readFile(*abiPath, "abi file");
    auto const& address = positionals[0];
    auto const& method = positionals[1];
    std::vector<std::string> methodArgs(positionals.begin() + 2, positionals.end());
    auto facts = chainFacts(_connection);
    auto sender = makeSender(_connection, facts, _args);
    Abi abi(facts.sm);
    auto data = abi.encodeMethod(abiText, method, argsToJsonArray(methodArgs));
    bool json = wantJson(_args.flag("json"));
    if (_mode == TxMode::Sign)
    {
        auto signedTx = sender->sign(address, std::move(data), "", sendOptionsFrom(_args));
        if (json)
        {
            Json::Value value;
            value["txHash"] = signedTx.hash;
            value["raw"] = "0x" + signedTx.hex;
            printJson(_out, value);
        }
        else
        {
            _out << "0x" << signedTx.hex << '\n';
        }
        return c_exitOk;
    }
    if (send)
    {
        auto receipt = sender->send(address, std::move(data), "", sendOptionsFrom(_args));
        printReceipt(_out, receipt, json);
        return receipt.ok() ? c_exitOk : c_exitChecksFailed;
    }
    auto receipt = sender->call(address, std::move(data));
    std::string decoded;
    if (receipt.ok())
    {
        auto outputHex =
            receipt.output.starts_with("0x") ? receipt.output.substr(2) : receipt.output;
        decoded = outputHex.empty() ? "[]" : abi.decodeOutput(abiText, method, fromHex(outputHex));
    }
    if (json)
    {
        Json::Value value;
        value["status"] = receipt.status;
        value["output"] = receipt.output;
        value["decoded"] =
            decoded.empty() ? Json::Value(Json::nullValue) : parseJson(decoded, "decoded");
        printJson(_out, value);
    }
    else
    {
        // decoded output and the receipt message are chain data: sanitize like printRows does
        _out << sanitizeForTerminal(receipt.ok() ?
                                        decoded :
                                        "call failed, status " + std::to_string(receipt.status) +
                                            " " + receipt.message)
             << '\n';
    }
    return receipt.ok() ? c_exitOk : c_exitChecksFailed;
}

int runTxGet(Connection const& _connection, Args const& _args, std::ostream& _out)
{
    if (_args.positionals().empty())
    {
        throw OpsError(c_exitUsage, "usage: tx get <txHash> [--proof]");
    }
    bool proof = _args.flag("proof");
    auto merged =
        fetchTransaction(_connection.call, _connection.group, _args.positionals()[0], proof);
    if (merged["receipt"].isObject())
    {
        merged["receipt"] = withRevertMessage(Receipt::fromJson(merged["receipt"])).raw;
    }
    if (wantJson(_args.flag("json")))
    {
        printJson(_out, merged);
        return c_exitOk;
    }
    auto receipt = Receipt::fromJson(merged["receipt"]);
    std::vector<std::pair<std::string, std::string>> rows;
    rows.emplace_back("from", merged["transaction"].get("from", "").asString());
    rows.emplace_back("to", merged["transaction"].get("to", "").asString());
    if (proof)
    {
        rows.emplace_back("txProof",
            std::to_string(merged["transaction"].get("txProof", Json::arrayValue).size()) +
                " nodes");
        // getTransactionReceipt fills txReceiptProof; receiptProof is an always-empty
        // compatibility field (JsonRpcImpl_2_0.cpp)
        rows.emplace_back("receiptProof",
            std::to_string(merged["receipt"].get("txReceiptProof", Json::arrayValue).size()) +
                " nodes");
    }
    rows.emplace_back("block", std::to_string(receipt.blockNumber));
    rows.emplace_back(
        "status", std::to_string(receipt.status) + (receipt.ok() ? " ok" : " FAILED"));
    rows.emplace_back("gasUsed", receipt.gasUsed);
    rows.emplace_back("output", receipt.output);
    if (!receipt.message.empty())
    {
        rows.emplace_back("message", receipt.message);
    }
    printRows(_out, rows);
    return c_exitOk;
}

int runTx(Args const& _args, std::ostream& _out, std::ostream& _err)
{
    (void)_err;
    if (_args.positionals().empty())
    {
        throw OpsError(c_exitUsage, "usage: tx smoke|deploy|call|send|sign|raw|get ...");
    }
    auto connection = connect(connectOptionsFrom(_args));
    return runTxOn(connection, _args, _out);
}

}  // namespace

int runTxOn(Connection const& _connection, Args const& _args, std::ostream& _out)
{
    try
    {
        return runTxDispatch(_connection, _args, _out);
    }
    catch (OpsRpcError const& e)
    {
        return printRpcRefusal(_out, e, wantJson(_args.flag("json")));
    }
}

namespace
{
int runTxDispatch(Connection const& _connection, Args const& _args, std::ostream& _out)
{
    if (_args.positionals().empty())
    {
        throw OpsError(c_exitUsage, "usage: tx smoke|deploy|call|send|sign|raw|get ...");
    }
    auto sub = _args.positionals()[0];
    std::vector<std::string> rest(_args.positionals().begin() + 1, _args.positionals().end());
    // re-wrap the remaining positionals; options are shared
    Args subArgs = _args.withPositionals(rest);
    if (sub == "smoke")
    {
        return runSmokeCmd(_connection, subArgs, _out);
    }
    if (sub == "deploy")
    {
        return runDeploy(_connection, subArgs, _out);
    }
    if (sub == "call")
    {
        return runCallOrSend(_connection, subArgs, _out, TxMode::Call);
    }
    if (sub == "send")
    {
        return runCallOrSend(_connection, subArgs, _out, TxMode::Send);
    }
    if (sub == "sign")
    {
        return runCallOrSend(_connection, subArgs, _out, TxMode::Sign);
    }
    if (sub == "raw")
    {
        return runRaw(_connection, subArgs, _out);
    }
    if (sub == "get")
    {
        return runTxGet(_connection, subArgs, _out);
    }
    throw OpsError(c_exitUsage, "unknown tx subcommand: " + sub);
}
}  // namespace

void registerTxCommand()
{
    registerCommand("tx",
        Command{"smoke (deploy HelloWorld, set, get), deploy/call/send with an ABI, raw signed "
                "transactions, get with proof (console: deploy, call, sendTransaction, "
                "getTransactionByHash/getTransactionReceipt)",
            "smoke | deploy <bin|@file> --abi <abi> [args..] [--value N] [--link "
            "/apps/<name>/<ver>] "
            "(console: deploy -l) | call <addr> --abi <abi> <fn> [args..] | send <addr> --abi "
            "<abi> "
            "<fn> [args..] [--value N] [--nonce S] [--block-limit N] | sign <addr> --abi <abi> "
            "<fn> "
            "[args..] (sign only, prints the raw hex) | raw <hex|@file> (console: sendTransaction "
            "with a signed tx; EL modes: eth_sendRawTransaction on the web3 listener, receipt "
            "polled up to --wait 60 s) | get <hash> [--proof]   [--account <pem|hex-key>] "
            "[--mode none|el|opstack-el] [--web3 host:port] [--node-dir "
            "<dir> | --rpc <host:port>] [--json]",
            {"proof"}, runTx});
}
}  // namespace bcos::ops
