/**
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @file CommandHelper.h
 * @author: yujiechen
 * @date 2021-06-10
 */
#pragma once
#include <iostream>
#include <memory>
namespace bcos
{
namespace initializer
{
void printVersion();
void showNodeVersionMetric();
void initCommandLine(int argc, char* argv[]);

struct Params
{
    std::string configFilePath;
    std::string genesisFilePath;
    std::string snapshotPath;  // import from or export to
    float txSpeed;
    enum class operation : int
    {
        None = 0,
        Prune = 1,
        Snapshot = 1 << 1,
        SnapshotWithoutTxAndReceipt = 1 << 2,
        ImportSnapshot = 1 << 3,
    } op;
    bool hasOp(operation op) const
    {
        return (static_cast<int>(this->op) & static_cast<int>(op)) != 0;
    }
};

inline Params::operation operator|(Params::operation left, Params::operation right)
{
    return static_cast<Params::operation>(static_cast<int>(left) | static_cast<int>(right));
}

inline Params::operation operator&(Params::operation left, Params::operation right)
{
    return static_cast<Params::operation>(static_cast<int>(left) & static_cast<int>(right));
}

Params initAirNodeCommandLine(int argc, const char* argv[], bool _autoSendTx);
}  // namespace initializer
}  // namespace bcos