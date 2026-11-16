/*
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
 */

#pragma once

// Deprecated forwarding header. AwaitableValue was renamed to Immediate in 3.18.0 (#5705); this
// header keeps out-of-tree consumers of the installed <bcos-task/AwaitableValue.h> compiling for
// one release cycle, with a deprecation warning at each use of the old name. It goes away in
// 3.19: include <bcos-task/Immediate.h> and spell the type bcos::task::Immediate.
#include "Immediate.h"

namespace bcos::task
{
template <class Value>
using AwaitableValue
    [[deprecated("renamed to bcos::task::Immediate; include <bcos-task/Immediate.h>")]] =
        Immediate<Value>;
}  // namespace bcos::task
