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
 * @brief the chain's feature_* / bugfix_* switches as the ops tool sees them: the full list
 *        from ledger::Features, each one's on/off state read through getSystemConfigByKey,
 *        and batch enabling in dependency order (a switch only ever goes on).
 * @file FeatureFlags.h
 */
#pragma once

#include "PresetCall.h"
#include "bcos-ops/RpcCall.h"
#include <string>
#include <vector>

namespace bcos::ops
{
struct FeatureState
{
    std::string name;
    bool enabled = false;
    bool deprecated = false;  // accepted by the chain but drives nothing
    Json::Value toJson() const;
};

/// every flag name the node accepts (Features::featureKeys(), reserved bits excluded)
std::vector<std::string> featureNames();
/// the flags that must be on before _name (Features::validate)
std::vector<std::string> featureDependencies(std::string const& _name);
/// _requested plus their dependencies, dependencies first, duplicates removed; throws
/// OpsError{1} on an unknown name
std::vector<std::string> featureEnableOrder(std::vector<std::string> const& _requested);
/// one getSystemConfigByKey per flag; a missing key (RPC error) reads as off
std::vector<FeatureState> readFeatureStates(RpcCall const& _call, std::string const& _group);

struct FeatureEnableStep
{
    std::string name;
    std::string action;  // skipped (already on) | enabled | failed
    std::optional<PresetResult> result;
    Json::Value toJson() const;
};
/// enables _requested in featureEnableOrder, skipping flags already on; stops at the first
/// failure (later flags would fail their dependency check anyway)
std::vector<FeatureEnableStep> enableFeatures(RpcCall const& _call, std::string const& _group,
    PresetRunner& _runner, std::vector<std::string> const& _requested);
}  // namespace bcos::ops
