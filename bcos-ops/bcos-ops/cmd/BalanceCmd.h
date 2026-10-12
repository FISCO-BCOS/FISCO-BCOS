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
 * @brief `balance get|add|sub|transfer|register-caller|unregister-caller|list-caller|enable`
 *        and `auth <read>|<write>` over an already open Connection (the entries the unit tests
 *        drive with a FakeRpc)
 * @file BalanceCmd.h
 */
#pragma once

#include "bcos-ops/Args.h"
#include "bcos-ops/RpcCall.h"
#include <iosfwd>

namespace bcos::ops
{
/// _args.positionals()[0] is the sub-subcommand; returns the process exit code
int runBalanceOn(Connection const& _connection, Args const& _args, std::ostream& _out);
int runAuthOn(Connection const& _connection, Args const& _args, std::ostream& _out);
}  // namespace bcos::ops
