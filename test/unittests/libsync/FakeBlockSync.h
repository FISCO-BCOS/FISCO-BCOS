/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */

/**
 * @brief:
 * @file: FakeBlockSync.cpp
 * @author: yujiechen
 * @date: 2018-10-10
 */
#pragma once
#include <libethcore/Block.h>
#include <libsync/SyncInterface.h>
#include <libsync/SyncStatus.h>
#include <memory>
using namespace dev::sync;
using namespace dev::eth;

namespace dev
{
namespace test
{
/// simple fake of blocksync
class FakeBlockSync : public SyncInterface
{
public:
    void start() override {}
    void stop() override {}
    SyncStatus status() const override { return m_syncStatus; }
    std::string const syncInfo() const override { return m_syncInfo; }
    bool isSyncing() const override { return m_isSyncing; }
    bool blockNumberFarBehind() const override { return false; }
    void setSyncing(bool syncing)
    {
        m_isSyncing = syncing;
        if (m_isSyncing)
        {
            m_syncStatus.state = SyncState::Downloading;
        }
        else
        {
            m_syncStatus.state = SyncState::Idle;
        }
    }
    PROTOCOL_ID const& protocolId() const override { return m_protocolId; };
    void setProtocolId(PROTOCOL_ID const _protocolId) override { m_protocolId = _protocolId; };
    void noteSealingBlockNumber(int64_t) override{};

    void registerConsensusVerifyHandler(std::function<bool(dev::eth::Block const&)>) override{};
    bool syncTreeRouterEnabled() override { return m_syncTreeRouterEnabled; }
    void setSyncTreeRouterEnabled(bool const& _syncTreeRouterEnabled)
    {
        m_syncTreeRouterEnabled = _syncTreeRouterEnabled;
    }

private:
    SyncStatus m_syncStatus;
    bool m_isSyncing;
    Block m_latestSentBlock;
    PROTOCOL_ID m_protocolId;
    std::string m_syncInfo;
    bool m_syncTreeRouterEnabled = false;
};
}  // namespace test
}  // namespace dev
