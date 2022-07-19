/**
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
 * @brief implementation for ed25519 keyPair algorithm
 * @file fast_sm2.h
 * @date 2022.01.17
 * @author yujiechen
 */
#pragma once

#ifdef WITH_SM2_OPTIMIZE
#include <wedpr-crypto/WedprUtilities.h>

namespace bcos
{
namespace crypto
{
// C interface for 'fast_sm2_sign'.
int8_t fast_sm2_sign(const CInputBuffer* raw_private_key, const CInputBuffer* raw_public_key,
    const CInputBuffer* raw_message_hash, COutputBuffer* output_signature);

// C interface for 'fast_sm2_verify'.
int8_t fast_sm2_verify(const CInputBuffer* raw_public_key, const CInputBuffer* raw_message_hash,
    const CInputBuffer* raw_signature);

// C interface for 'fast_sm2_verify'.
int8_t fast_sm2_derive_public_key(
    const CInputBuffer* raw_private_key, COutputBuffer* output_public_key);
}  // namespace crypto
}  // namespace bcos
#endif