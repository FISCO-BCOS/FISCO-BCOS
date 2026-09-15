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

namespace bcos::codec::rlp
{
size_t length(const protocol::EthLogData& _log) noexcept
{
    return length(_log.address, _log.topics, _log.data);
}
void encode(bcos::bytes& _out, const protocol::EthLogData& _log) noexcept
{
    encode(_out, _log.address, _log.topics, _log.data);
}
void decode(bcos::bytesRef& _in, protocol::EthLogData& _log)
{
    decode(_in, _log.address, _log.topics, _log.data);
}
}  // namespace bcos::codec::rlp

namespace bcos::protocol
{
void EthLog::rlpEncode(bcos::bytes& out) const
{
    codec::rlp::encode(out, m_data);
}

void EthLog::rlpDecode(bcos::bytesConstRef data)
{
    codec::rlp::decodeExact(data, m_data);
}

size_t length(const EthLogData& _log) noexcept
{
    return codec::rlp::length(_log);
}
void encode(bcos::bytes& _out, const EthLogData& _log) noexcept
{
    codec::rlp::encode(_out, _log);
}
void decode(bcos::bytesRef& _in, EthLogData& _log)
{
    codec::rlp::decode(_in, _log);
}
}  // namespace bcos::protocol
