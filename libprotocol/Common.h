/*
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
 * @author wheatli
 * @date 2018.8.27
 * @modify add asAddress and fromAddress
 *
 */

#pragma once
#include "CommonProtocolType.h"
#include <libutilities/FixedBytes.h>
#include <libutilities/JsonDataConvertUtility.h>
#include <stdint.h>

#include <functional>
#include <string>

namespace bcos
{
namespace protocol
{
// Convert from a 256-bit integer stack/memory entry into a 160-bit Address hash.
// Currently we just pull out the right (low-order in BE) 160-bits.
inline Address asAddress(u256 _item)
{
    return right160(h256(_item));
}

inline u256 fromAddress(Address _a)
{
    return (u160)_a;
}

/// Convert the given string into an address.
Address toAddress(std::string const& _address);

inline Address jsonStringToAddress(std::string const& _s)
{
    return protocol::toAddress(_s);
}

/// Convert to a block number, a bit like jsonStringToInt, except that it correctly recognises
inline BlockNumber jsonStringToBlockNumber(std::string const& _blockNumberStr)
{
    return jsonStringToInt(_blockNumberStr);
}
}  // namespace protocol
}  // namespace bcos
