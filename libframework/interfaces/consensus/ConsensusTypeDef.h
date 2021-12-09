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
 * @brief data type for consensus module
 * @file ConsensusTypeDef.h
 * @author: yujiechen
 * @date 2021-04-09
 */
#pragma once
#include "../../interfaces/crypto/KeyInterface.h"
namespace bcos
{
namespace consensus
{
using NodeType = uint64_t;
using IndexType = uint64_t;
using ViewType = uint64_t;
}  // namespace consensus
}  // namespace bcos
