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
 * @file EthWithdrawal.cpp
 * @brief EthWithdrawal — Ethereum withdrawal RLP codec implementation (EIP-4895)
 * @date 2026/8/18
 */
#include "EthWithdrawal.h"

using namespace bcos;
using namespace bcos::codec::rlp;

namespace bcos::protocol
{
void EthWithdrawal::rlpEncode(bcos::bytes& out) const
{
    codec::rlp::encode(out, m_data);
}

bcos::Error::UniquePtr EthWithdrawal::rlpDecode(bcos::bytesConstRef data)
{
    // The codec's decode takes a mutable bytesRef& (it advances a view cursor); the bytes
    // themselves are never written, so a single copy into a mutable buffer is enough.
    auto mutableData = data.toBytes();
    bytesRef in(mutableData.data(), mutableData.size());
    return codec::rlp::decode(in, m_data);
}
}  // namespace bcos::protocol
