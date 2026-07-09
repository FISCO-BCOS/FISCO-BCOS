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
 * @file FlatToMPT.cpp
 * @brief Flat-KV → MPT migration: preheat-manifest schema implementation (spec §5.11)
 */
#include "FlatToMPT.h"
#include <bcos-utilities/BoostLog.h>

namespace bcos::ledger::mpt
{

std::string PreheatManifest::makeKey(bcos::Address const& addr)
{
    std::string key;
    key.reserve(keyPrefix.size() + (bcos::Address::SIZE * 2));
    key.append(keyPrefix);
    key.append(addr.hex());  // toHex uses hex_lower: 40 lowercase chars, no 0x
    return key;
}

bcos::task::Task<PreheatManifest::Record> PreheatManifest::read(
    BackendReader const& reader, bcos::Address const& addr)
{
    auto raw = co_await reader(makeKey(addr));
    if (!raw)
    {
        co_return Record{Lookup::Miss, {}};
    }
    if (raw->size() != bcos::h256::SIZE)
    {
        // Fail loud in the log, but do not throw: the caller treats Corrupt as a miss and
        // rebuilds from the flat scan, so one mis-written record cannot deadlock the chain.
        BCOS_LOG(WARNING) << LOG_BADGE("PreheatManifest")
                          << LOG_DESC("corrupt manifest record ignored; rebuilding from flat scan")
                          << LOG_KV("addr", addr.hex()) << LOG_KV("size", raw->size());
        co_return Record{Lookup::Corrupt, {}};
    }
    co_return Record{Lookup::Hit, bcos::h256{bcos::bytesConstRef{raw->data(), raw->size()}}};
}

bcos::task::Task<void> PreheatManifest::remove(
    BackendDeleter const& deleter, bcos::Address const& addr)
{
    co_await deleter(makeKey(addr));
}

}  // namespace bcos::ledger::mpt
