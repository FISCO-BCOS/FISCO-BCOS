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
 * @file PreheatManifest.cpp
 * @brief Preheat-manifest KV schema and read/remove interface (spec §5.11)
 */
#include "PreheatManifest.h"
#include "Errors.h"
#include <bcos-utilities/Exceptions.h>
#include <boost/throw_exception.hpp>

namespace bcos::ledger::mpt
{

std::string PreheatManifest::makeKey(bcos::Address const& addr)
{
    std::string key;
    key.reserve(keyPrefix.size() + bcos::Address::SIZE * 2);
    key.append(keyPrefix);
    key.append(addr.hex());  // toHex uses hex_lower: 40 lowercase chars, no 0x
    return key;
}

bcos::task::Task<std::optional<bcos::h256>> PreheatManifest::read(
    BackendReader const& reader, bcos::Address const& addr)
{
    auto raw = co_await reader(makeKey(addr));
    if (!raw)
    {
        co_return std::nullopt;
    }
    if (raw->size() != bcos::h256::SIZE)
    {
        BOOST_THROW_EXCEPTION(MPTInvariantViolation{} << bcos::errinfo_comment(
                                  "PreheatManifest: record value must be a 32-byte storage root"));
    }
    co_return bcos::h256{bcos::bytesConstRef{raw->data(), raw->size()}};
}

bcos::task::Task<void> PreheatManifest::remove(
    BackendDeleter const& deleter, bcos::Address const& addr)
{
    co_await deleter(makeKey(addr));
}

}  // namespace bcos::ledger::mpt
