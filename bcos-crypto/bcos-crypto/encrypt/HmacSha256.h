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
 * @file HmacSha256.h
 * @brief HMAC-SHA256 (RFC 2104) — required by RLPx ECIES handshake message
 *        authentication. (Note: the RLPx frame egress/ingress MACs are a
 *        keccak-256 running state, not HMAC-SHA256.)
 * @date 2026/8/18
 */
#pragma once

#include <bcos-utilities/Common.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <cstddef>

namespace bcos::crypto
{
// HMAC-SHA256 over up to three concatenated data segments (RLPx needs
// HMAC(km, iv || ciphertext || mac-extra-data)).
bcos::bytes hmacSha256(bytesConstRef _key, bytesConstRef _data1);
bcos::bytes hmacSha256(bytesConstRef _key, bytesConstRef _data1, bytesConstRef _data2);
bcos::bytes hmacSha256(
    bytesConstRef _key, bytesConstRef _data1, bytesConstRef _data2, bytesConstRef _data3);
}  // namespace bcos::crypto
