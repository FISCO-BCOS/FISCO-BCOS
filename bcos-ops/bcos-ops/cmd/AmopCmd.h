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
 * @brief `fisco-bcos amop sub <topic> [--count N] [--wait S]` / `amop pub <topic> <msg> [--wait S]`
 *        (java-sdk AmopTest; console has no AMOP command). The entry point below takes the
 *        transport so tests drive it with a fake.
 * @file AmopCmd.h
 */
#pragma once

#include "bcos-ops/Args.h"
#include "bcos-ops/amop/AmopClient.h"
#include <atomic>
#include <iosfwd>

namespace bcos::ops
{
/// _stop ends `sub --count 0` (SIGINT in the CLI, the test fake otherwise). Exit codes:
/// 0 = N messages received / publish answered; 2 = deadline passed or the node refused the
/// publish; 1 = usage.
int runAmopOn(AmopTransport& _transport, Args const& _args, std::ostream& _out, std::ostream& _err,
    std::atomic<bool> const& _stop);
}  // namespace bcos::ops
