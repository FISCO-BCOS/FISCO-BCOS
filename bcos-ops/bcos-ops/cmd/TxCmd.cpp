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
#include "bcos-ops/Args.h"
#include "bcos-ops/Cli.h"
#include "bcos-ops/Connect.h"
#include "bcos-ops/OpsError.h"
#include "bcos-ops/Output.h"
#include "bcos-ops/collect/RpcCollector.h"
#include "bcos-ops/tx/Abi.h"
#include "bcos-ops/tx/Account.h"
#include "bcos-ops/tx/Smoke.h"
#include "bcos-ops/tx/TxSender.h"
#include <bcos-utilities/DataConvertUtility.h>
#include <fstream>
#include <ostream>
#include <sstream>

namespace bcos::ops
{
namespace
{
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

struct ChainFacts
{
    std::string chainId;
    bool sm = false;
    std::optional<bool> authCheck;
};

/// chain id / crypto type / auth switch from getGroupInfo (node dir as fallback)
ChainFacts chainFacts(Connection const& _connection)
{
    ChainFacts facts;
    if (_connection.nodeDir)
    {
        facts.chainId = _connection.nodeDir->chainId;
        facts.sm = _connection.nodeDir->smCrypto;
        facts.authCheck = _connection.nodeDir->authCheck;
    }
    try
    {
        Json::Value params(Json::arrayValue);
        params.append(_connection.group);
        auto info = _connection.call("getGroupInfo", params);
        facts.chainId = info["chainID"].asString();
        if (!info["nodeList"].empty())
        {
            auto ini = parseJson(info["nodeList"][0]["iniConfig"].asString(), "getGroupInfo");
            facts.sm = ini["smCryptoType"].asBool();
            facts.authCheck = ini["isAuthCheck"].asBool();
        }
    }
    catch (std::exception const&)
    {
        if (facts.chainId.empty())
        {
            throw;
        }
    }
    return facts;
}

std::unique_ptr<TxSender> makeSender(
    Connection const& _connection, ChainFacts const& _facts, Args const& _args)
{
    auto account = makeAccount(_facts.sm, _args.option("account"));
    return std::make_unique<TxSender>(
        _connection.call, _connection.group, _facts.chainId, _facts.sm, std::move(account));
}

void printReceipt(std::ostream& _out, Receipt const& _receipt, bool _json)
{
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
    auto result = runSmoke(*sender, facts.sm, facts.authCheck);
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
            _out << std::left << std::setw(8) << step.name << std::setw(14)
                 << (step.txHash.empty() ? "-" : abridged(step.txHash, 6))
                 << (step.ok ? "ok   " : "FAIL ") << step.detail << '\n';
        }
        if (!result.ok)
        {
            _out << "smoke failed: reason=" << result.reason << '\n';
        }
    }
    return result.exitCode();
}

int runDeploy(Connection const& _connection, Args const& _args, std::ostream& _out)
{
    auto const& positionals = _args.positionals();
    if (positionals.empty())
    {
        throw OpsError(c_exitUsage, "usage: tx deploy <bin-file|hex> --abi <abi-file> [args...]");
    }
    auto abiPath = _args.option("abi");
    if (!abiPath)
    {
        throw OpsError(c_exitUsage, "tx deploy needs --abi <abi-file>");
    }
    std::string bin = positionals[0];
    if (std::ifstream(bin).good())
    {
        bin = readFile(bin, "bin file");
    }
    while (!bin.empty() && (bin.back() == '\n' || bin.back() == '\r' || bin.back() == ' '))
    {
        bin.pop_back();
    }
    if (bin.starts_with("0x"))
    {
        bin = bin.substr(2);
    }
    auto abiText = readFile(*abiPath, "abi file");
    auto facts = chainFacts(_connection);
    auto sender = makeSender(_connection, facts, _args);
    Abi abi(facts.sm);
    std::vector<std::string> ctorArgs(positionals.begin() + 1, positionals.end());
    auto data = abi.encodeConstructor(abiText, bin, argsToJsonArray(ctorArgs));
    auto receipt = sender->send("", std::move(data), abiText);
    printReceipt(_out, receipt, wantJson(_args.flag("json")));
    return receipt.ok() ? c_exitOk : c_exitChecksFailed;
}

int runCallOrSend(Connection const& _connection, Args const& _args, std::ostream& _out, bool _send)
{
    auto const& positionals = _args.positionals();
    if (positionals.size() < 2)
    {
        throw OpsError(c_exitUsage, std::string("usage: tx ") + (_send ? "send" : "call") +
                                        " <address> --abi <abi-file> <function> [args...]");
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
    if (_send)
    {
        auto receipt = sender->send(address, std::move(data), "");
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
        _out << (receipt.ok() ? decoded :
                                "call failed, status " + std::to_string(receipt.status) + " " +
                                    receipt.message)
             << '\n';
    }
    return receipt.ok() ? c_exitOk : c_exitChecksFailed;
}

int runGet(Connection const& _connection, Args const& _args, std::ostream& _out)
{
    if (_args.positionals().empty())
    {
        throw OpsError(c_exitUsage, "usage: tx get <txHash>");
    }
    auto merged = fetchTransaction(_connection.call, _connection.group, _args.positionals()[0]);
    if (wantJson(_args.flag("json")))
    {
        printJson(_out, merged);
        return c_exitOk;
    }
    auto receipt = Receipt::fromJson(merged["receipt"]);
    std::vector<std::pair<std::string, std::string>> rows;
    rows.emplace_back("from", merged["transaction"].get("from", "").asString());
    rows.emplace_back("to", merged["transaction"].get("to", "").asString());
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
        throw OpsError(c_exitUsage, "usage: tx smoke|deploy|call|send|get ...");
    }
    auto sub = _args.positionals()[0];
    std::vector<std::string> rest(_args.positionals().begin() + 1, _args.positionals().end());
    // re-wrap the remaining positionals; options are shared
    Args subArgs = _args.withPositionals(rest);
    auto connection = connect(connectOptionsFrom(_args));
    if (sub == "smoke")
    {
        return runSmokeCmd(connection, subArgs, _out);
    }
    if (sub == "deploy")
    {
        return runDeploy(connection, subArgs, _out);
    }
    if (sub == "call")
    {
        return runCallOrSend(connection, subArgs, _out, false);
    }
    if (sub == "send")
    {
        return runCallOrSend(connection, subArgs, _out, true);
    }
    if (sub == "get")
    {
        return runGet(connection, subArgs, _out);
    }
    throw OpsError(c_exitUsage, "unknown tx subcommand: " + sub);
}

struct TxRegister
{
    TxRegister()
    {
        registerCommand(
            "tx", Command{"smoke (deploy HelloWorld, set, get) or deploy/call/send/get with an ABI",
                      "smoke | deploy <bin> --abi <abi> [args..] | call <addr> --abi <abi> <fn> "
                      "[args..] | "
                      "send <addr> --abi <abi> <fn> [args..] | get <hash>   [--account <pem>] "
                      "[--node-dir "
                      "<dir> | --rpc <host:port>] [--json]",
                      {}, runTx});
    }
} s_txRegister;
}  // namespace
}  // namespace bcos::ops
