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

namespace bcos::codec::rlp
{
size_t length(const protocol::EthWithdrawalData& _withdrawal) noexcept
{
    return length(
        _withdrawal.index, _withdrawal.validatorIndex, _withdrawal.address, _withdrawal.amount);
}
void encode(bcos::bytes& _out, const protocol::EthWithdrawalData& _withdrawal) noexcept
{
    encode(_out, _withdrawal.index, _withdrawal.validatorIndex, _withdrawal.address,
        _withdrawal.amount);
}
void decode(bcos::bytesRef& _in, protocol::EthWithdrawalData& _withdrawal)
{
    decode(_in, _withdrawal.index, _withdrawal.validatorIndex, _withdrawal.address,
        _withdrawal.amount);
}
}  // namespace bcos::codec::rlp

namespace bcos::protocol
{
void EthWithdrawal::rlpEncode(bcos::bytes& out) const
{
    codec::rlp::encode(out, m_data);
}

void EthWithdrawal::rlpDecode(bcos::bytesConstRef data)
{
    codec::rlp::decodeExact(data, m_data);
}

size_t length(const EthWithdrawalData& _withdrawal) noexcept
{
    return codec::rlp::length(_withdrawal);
}
void encode(bcos::bytes& _out, const EthWithdrawalData& _withdrawal) noexcept
{
    codec::rlp::encode(_out, _withdrawal);
}
void decode(bcos::bytesRef& _in, EthWithdrawalData& _withdrawal)
{
    codec::rlp::decode(_in, _withdrawal);
}
}  // namespace bcos::protocol
