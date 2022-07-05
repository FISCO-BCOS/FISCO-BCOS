/*
 *  Copyright (C) 2022 FISCO BCOS.
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
 * @brief interface definition of ExecutiveFlow
 * @file ExecutiveStackFlow.h
 * @author: jimmyshi
 * @date: 2022-03-22
 */

#pragma once

#include "../mock/MockExecutiveFactory.h"
#include "ExecutiveFlowInterface.h"
#include "ExecutiveState.h"
#include <tbb/concurrent_unordered_map.h>
#include <atomic>
#include <stack>

using namespace std;
using namespace bcos;
using namespace bcos::executor;

namespace std::test
{

}
