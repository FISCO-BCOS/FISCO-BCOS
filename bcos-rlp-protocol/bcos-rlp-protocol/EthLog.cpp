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
 * @file EthLog.cpp
 * @brief EthLog — Ethereum transaction-log RLP codec implementation
 * @date 2026/8/18
 */
#include "EthLog.h"

using namespace bcos;
using namespace bcos::codec::rlp;

namespace bcos::protocol
{
void EthLog::rlpEncode(bcos::bytes& out) const
{
    codec::rlp::encode(out, m_data);
}

bcos::Error::UniquePtr EthLog::rlpDecode(bcos::bytesConstRef data)
{
    // The codec's decode only advances a view cursor and never writes the buffer, so
    // take the view directly; the const_cast is confined to this read-only entry point.
    bytesRef in(const_cast<bcos::byte*>(data.data()), data.size());
    if (auto err = codec::rlp::decode(in, m_data))
    {
        return err;
    }
    // geth's rlp.DecodeBytes rejects trailing bytes (ErrMoreThanOneValue); mirror that so
    // two distinct wire encodings cannot map to the same decoded object.
    if (!in.empty())
    {
        return BCOS_ERROR_UNIQUE_PTR(
            DecodingError::UnexpectedListElements, "trailing bytes after top-level RLP item");
    }
    return nullptr;
}
}  // namespace bcos::protocol
