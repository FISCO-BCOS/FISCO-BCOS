/**
 *  Copyright (C) 2020 FISCO BCOS.
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
 * @brief: calc trie hash with merkle tree
 *
 * @file: ParallelMerkleProof.h
 * @author: darrenyin
 * @date 2019-09-24
 */
#pragma once

#include "FixedHash.h"
#include <vector>

namespace bcos
{
h256 getMerkleProofRoot(const std::vector<bcos::bytes>& bytesCaches);

void getMerkleProof(const std::vector<bcos::bytes>& bytesCaches,
    std::shared_ptr<std::map<std::string, std::vector<std::string>>> _parent2ChildList);

}  // namespace bcos
