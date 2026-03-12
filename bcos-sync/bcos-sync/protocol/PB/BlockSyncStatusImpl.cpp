/**
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
 * @brief implementation for the block sync status packet
 * @file BlockSyncStatusImpl.cpp
 * @author: yujiechen
 * @date 2021-05-23
 */
#include "BlockSyncStatusImpl.h"
#include "bcos-sync/utilities/Common.h"
#include <bcos-protocol/Common.h>
#include <bcos-utilities/Common.h>

using namespace bcos;
using namespace bcos::sync;
using namespace bcos::protocol;
using namespace bcos::crypto;

// maximum allowed time drift from local UTC: 24 hours in milliseconds
static constexpr std::int64_t MAX_TIME_DRIFT_MS = 24 * 60 * 60 * 1000LL;

void BlockSyncStatusImpl::decode(bytesConstRef _data)
{
    BlockSyncMsgImpl::decode(_data);
    deserializeObject();
}

void BlockSyncStatusImpl::deserializeObject()
{
    auto const& hashData = m_syncMessage->hash();
    if (hashData.size() != HashType::SIZE)
    {
        BOOST_THROW_EXCEPTION(PBObjectDecodeException()
                              << errinfo_comment("BlockSyncStatus: invalid hash size, expected " +
                                                 std::to_string(HashType::SIZE) + " got " +
                                                 std::to_string(hashData.size())));
    }
    m_hash = HashType((byte const*)hashData.data(), HashType::SIZE);
    // swap with empty string to reliably release protobuf's underlying allocation
    std::string().swap(*m_syncMessage->mutable_hash());

    auto const& genesisHashData = m_syncMessage->genesishash();
    if (genesisHashData.size() != HashType::SIZE)
    {
        BOOST_THROW_EXCEPTION(
            PBObjectDecodeException() << errinfo_comment(
                "BlockSyncStatus: invalid genesis hash size, expected " +
                std::to_string(HashType::SIZE) + " got " + std::to_string(genesisHashData.size())));
    }
    m_genesisHash = HashType((byte const*)genesisHashData.data(), HashType::SIZE);
    std::string().swap(*m_syncMessage->mutable_genesishash());

    auto rawTime = m_syncMessage->time();
    auto localTime = static_cast<std::int64_t>(utcTime());
    // Validate peer time: must be within ±24h of local UTC time
    if (rawTime >= localTime - MAX_TIME_DRIFT_MS && rawTime <= localTime + MAX_TIME_DRIFT_MS)
    {
        m_time = rawTime;
    }
    else
    {
        BLKSYNC_LOG(WARNING) << LOG_DESC("deserializeObject: peer time out of valid range")
                             << LOG_KV("peerTime", rawTime) << LOG_KV("localTime", localTime)
                             << LOG_KV("maxDriftMs", MAX_TIME_DRIFT_MS);
        m_time = 0;
    }
}
void BlockSyncStatusImpl::setHash(HashType const& _hash)
{
    m_hash = _hash;
    m_syncMessage->set_hash(_hash.data(), HashType::SIZE);
}

void BlockSyncStatusImpl::setGenesisHash(HashType const& _gensisHash)
{
    m_genesisHash = _gensisHash;
    m_syncMessage->set_genesishash(_gensisHash.data(), HashType::SIZE);
}

void BlockSyncStatusImpl::setTime(std::int64_t const time)
{
    m_time = time;
    m_syncMessage->set_time(time);
}