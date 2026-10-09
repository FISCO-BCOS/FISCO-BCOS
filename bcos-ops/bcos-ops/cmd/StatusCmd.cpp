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
 * @brief `fisco-bcos status`: one NodeStatus snapshot + checks, exit 2 on any failed check
 * @file StatusCmd.cpp
 */
#include "bcos-ops/Cli.h"
#include "bcos-ops/OpsError.h"
#include <ostream>

namespace bcos::ops
{
namespace
{
int runStatus(Args const& _args, std::ostream& _out, std::ostream& _err)
{
    (void)_args;
    (void)_out;
    _err << "status: not implemented yet\n";
    return c_exitUsage;
}

struct Register
{
    Register()
    {
        registerCommand("status",
            Command{"print the node status snapshot and run the health checks",
                "[--node-dir <dir> | --rpc <host:port>] [--json] [--stall-factor 2] [--max-lag 10]",
                {}, runStatus});
    }
} s_register;
}  // namespace
}  // namespace bcos::ops
