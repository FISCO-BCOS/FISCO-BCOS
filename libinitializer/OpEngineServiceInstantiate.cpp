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
 * @file OpEngineServiceInstantiate.cpp
 * @brief Production instantiation point for the OpEngineService template definitions.
 *
 * OpEngineService.h is declarations-only; the definitions live in OpEngineService.inl.
 * This TU instantiates the one specialization Initializer::init wires (MemPoolImpl +
 * GlobalStateStorage + OpSchedulerSeam), so the .inl is compiled exactly once.
 */
#include "GlobalStateStorageInitializer.h"
#include "bcos-mempool/MemPoolImpl.h"
#include <opstack-executor/OpSchedulerSeam.h>
#include <engine/bcos-engine/OpEngineService.inl>

template class bcos::engine::OpEngineService<bcos::txpool::MemPoolImpl,
    bcos::initializer::GlobalStateStorage,
    bcos::evm::engine::OpSchedulerSeam<bcos::initializer::GlobalStateStorage::ViewType>>;
