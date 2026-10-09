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
 * @file AdminMethods.cpp
 */
#include "AdminMethods.h"
#include <bcos-utilities/BoostLog.h>
#include <algorithm>

namespace bcos::rpc
{
namespace
{
constexpr std::array<std::pair<std::string_view, LogLevel>, 6> c_levelNames = {{
    {"trace", LogLevel::TRACE},
    {"debug", LogLevel::DEBUG},
    {"info", LogLevel::INFO},
    {"warning", LogLevel::WARNING},
    {"error", LogLevel::ERROR},
    {"fatal", LogLevel::FATAL},
}};

std::string levelName(LogLevel _level)
{
    for (auto const& [name, level] : c_levelNames)
    {
        if (level == _level)
        {
            return std::string(name);
        }
    }
    return "unknown";
}

std::optional<LogLevel> parseLevel(std::string _name)
{
    std::transform(_name.begin(), _name.end(), _name.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (auto const& [name, level] : c_levelNames)
    {
        if (name == _name)
        {
            return level;
        }
    }
    return std::nullopt;
}

Json::Value logLevelsJson()
{
    Json::Value result;
    result["global"] = levelName(c_fileLogLevel);
    result["modules"] = Json::Value(Json::objectValue);
    for (auto const& [module, level] : moduleLogLevels())
    {
        result["modules"][module] = levelName(level);
    }
    return result;
}

void invalidParams(RespFunc const& _respFunc, std::string _message)
{
    Json::Value empty;
    _respFunc(BCOS_ERROR_PTR(JsonRpcError::InvalidParams, std::move(_message)), empty);
}
}  // namespace

void registerAdminMethods(JsonRpcInterface& _rpc)
{
    _rpc.registerIpcOnlyMethod("admin_getLogLevel", [](Json::Value&, RespFunc _respFunc) {
        auto result = logLevelsJson();
        _respFunc(nullptr, result);
    });
    _rpc.registerIpcOnlyMethod("admin_setLogLevel", [](Json::Value& _params, RespFunc _respFunc) {
        if (!_params.isArray() || _params.empty() || !_params[0].isString())
        {
            invalidParams(_respFunc, "admin_setLogLevel expects [level] or [level, module]");
            return;
        }
        auto levelText = _params[0].asString();
        std::optional<std::string> module;
        if (_params.size() > 1 && _params[1].isString() && !_params[1].asString().empty())
        {
            module = _params[1].asString();
        }
        if (module)
        {
            if (!parseLogModule(*module))
            {
                invalidParams(_respFunc, "unknown module: " + *module +
                                             " (PBFT TXPOOL SYNC SCHEDULER EXECUTOR LEDGER RPC "
                                             "GATEWAY FRONT)");
                return;
            }
            if (levelText == "inherit")
            {
                resetModuleLogLevel(*module);
            }
            else
            {
                auto level = parseLevel(levelText);
                if (!level)
                {
                    invalidParams(_respFunc, "unknown level: " + levelText);
                    return;
                }
                setModuleLogLevel(*module, *level);
            }
        }
        else
        {
            auto level = parseLevel(levelText);
            if (!level)
            {
                invalidParams(_respFunc, "unknown level: " + levelText);
                return;
            }
            setFileLogLevel(*level);
        }
        RPC_IMPL_LOG(INFO) << LOG_DESC("LogLevelChanged") << LOG_KV("level", levelText)
                           << LOG_KV("module", module.value_or("global"));
        auto result = logLevelsJson();
        _respFunc(nullptr, result);
    });
}
}  // namespace bcos::rpc
