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
 * @file FeatureFlags.cpp
 */
#include "FeatureFlags.h"
#include "bcos-ops/OpsError.h"
#include <bcos-framework/ledger/Features.h>
#include <algorithm>
#include <set>

namespace bcos::ops
{
namespace
{
// Features::validate (bcos-framework/bcos-framework/ledger/Features.cpp) rejects a flag whose
// prerequisite is off; this table mirrors its dependencies.
struct Dependency
{
    std::string_view flag;
    std::string_view prerequisite;
};
constexpr std::array c_dependencies{
    Dependency{"feature_balance_precompiled", "feature_balance"},
    Dependency{"feature_balance_policy1", "feature_balance_precompiled"},
};
// Features::validate warns on this one: the account-table encoding is node-local now
constexpr std::string_view c_deprecated = "feature_raw_address";

void pushWithDependencies(
    std::string const& _name, std::set<std::string>& _seen, std::vector<std::string>& _order)
{
    if (_seen.contains(_name))
    {
        return;
    }
    for (auto const& dependency : featureDependencies(_name))
    {
        pushWithDependencies(dependency, _seen, _order);
    }
    _seen.insert(_name);
    _order.push_back(_name);
}

bool readEnabled(RpcCall const& _call, std::string const& _group, std::string const& _name)
{
    Json::Value params(Json::arrayValue);
    params.append(_group);
    params.append("");
    params.append(_name);
    try
    {
        return _call("getSystemConfigByKey", params).get("value", "").asString() == "1";
    }
    catch (OpsRpcError const& e)
    {
        // Ledger::asyncGetSystemConfigByKey: no row is GetStorageError (3008), a row whose
        // enable number is past the next block is ErrorArgument (3003, "Config not available");
        // both read as "not on yet" (3008 is also the ledger's catch-all for a storage
        // exception, which reads as off too). A timeout or a closed socket is a failure, as in
        // AuthMode.cpp: reading it as "off" would report every flag off and re-send enables
        if (e.code != 3008 && e.code != 3003)
        {
            throw;
        }
        return false;
    }
}
}  // namespace

Json::Value FeatureState::toJson() const
{
    Json::Value value;
    value["name"] = name;
    value["enabled"] = enabled;
    value["deprecated"] = deprecated;
    return value;
}

Json::Value FeatureEnableStep::toJson() const
{
    Json::Value value;
    value["name"] = name;
    value["action"] = action;
    if (result)
    {
        value["result"] = result->toJson();
    }
    return value;
}

std::vector<std::string> featureNames()
{
    std::vector<std::string> names;
    for (auto key : bcos::ledger::Features::featureKeys())
    {
        // featureKeys() walks the whole enum, including the reserved_removed_* placeholders
        // (Features.h: retired bits whose names string2Flag still accepts); setValueByKey would
        // write such a bit on chain for ever, so they are not offered
        if (std::string_view(key).starts_with("reserved_"))
        {
            continue;
        }
        names.emplace_back(key);
    }
    return names;
}

std::vector<std::string> featureDependencies(std::string const& _name)
{
    std::vector<std::string> required;
    for (auto const& dependency : c_dependencies)
    {
        if (dependency.flag == _name)
        {
            required.emplace_back(dependency.prerequisite);
        }
    }
    return required;
}

std::vector<std::string> featureEnableOrder(std::vector<std::string> const& _requested)
{
    auto known = featureNames();
    std::set<std::string> seen;
    std::vector<std::string> order;
    for (auto const& name : _requested)
    {
        if (std::ranges::find(known, name) == known.end())
        {
            throw OpsError(c_exitUsage, "unknown feature flag: " + name);
        }
        pushWithDependencies(name, seen, order);
    }
    return order;
}

std::vector<FeatureState> readFeatureStates(RpcCall const& _call, std::string const& _group)
{
    std::vector<FeatureState> states;
    for (auto const& name : featureNames())
    {
        states.push_back(
            FeatureState{name, readEnabled(_call, _group, name), name == c_deprecated});
    }
    return states;
}

std::vector<FeatureEnableStep> enableFeatures(RpcCall const& _call, std::string const& _group,
    PresetRunner& _runner, std::vector<std::string> const& _requested)
{
    auto const* setPreset = findPreset("sysconfig", "set");
    std::vector<FeatureEnableStep> steps;
    for (auto const& name : featureEnableOrder(_requested))
    {
        if (readEnabled(_call, _group, name))
        {
            steps.push_back(FeatureEnableStep{name, "skipped", std::nullopt});
            continue;
        }
        auto result = _runner.run(*setPreset, {name, "1"});
        bool ok = result.ok;
        steps.push_back(FeatureEnableStep{name, ok ? "enabled" : "failed", std::move(result)});
        if (!ok)
        {
            break;
        }
    }
    return steps;
}
}  // namespace bcos::ops
