/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file L2ConfigLoader.cpp
 * @brief Translation unit placeholder for L2ConfigLoaderImpl.
 *
 * L2ConfigLoaderImpl is a header-only template (L2ConfigLoader.h) parameterized
 * on the concrete state storage type. The production instantiation — wiring the
 * Scheduler's per-block state-storage handle into the L2 config reload hook —
 * lands with the A4 OpStackInitializer workflow, not this PR. PR-4 only
 * delivers the entry point (the template + interface) and its unit tests, so
 * there is no production instantiation to anchor in a .cpp yet.
 */
#include "L2ConfigLoader.h"
