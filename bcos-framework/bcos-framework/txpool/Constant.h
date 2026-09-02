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
 *
 * @file Constant.h
 * @author: asherli
 * @date: 2024-12-12
 */
#pragma once
#include <cstddef>
#include <cstdint>

// u256 with 0x prefix
constexpr size_t TRANSACTION_VALUE_MAX_LENGTH = 256 * 2 + 2;
// EIP-170
constexpr int MAX_CODE_SIZE = 0x6000;
// EIP 3860
constexpr int MAX_INITCODE_SIZE = 2 * MAX_CODE_SIZE;
namespace bcos::protocol
{
// The Web3 nonce window is expressed in blocks; a transaction whose nonce sits further than
// this many slots ahead of the committed account nonce is refused admission. Lives here rather
// than in bcos-txpool so the admission layer can read it without depending on a pool.
constexpr int64_t DEFAULT_BLOCK_LIMIT = 600;
constexpr uint64_t DEFAULT_WEB3_NONCE_CHECK_LIMIT = DEFAULT_BLOCK_LIMIT * 1000;
}  // namespace bcos::protocol
