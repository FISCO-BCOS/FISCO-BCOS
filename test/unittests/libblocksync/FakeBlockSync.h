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
#include <libblocksync/SyncInterface.h>
#include <memory>
using namespace dev::sync;

namespace dev
{
namespace test
{
/// simple fake of blocksync
class FakeBlockSync : public SyncInterface
{
    void start() override {}
    void stop() override {}
    SyncStatus status() const override {}
    bool isSyncing() const override {}
    h256 latestBlockSent() override {}
    void broadCastTransactions() override {}
    /// for p2p: broad cast transaction to specified nodes
    void sendTransactions(NodeList const& _nodes) override {}
    int16_t const& getProtocolId() const override{};
    void setProtocolId(int16_t const _protocolId) override{};
    void reset() override{};
    bool forceSync() override{};
};
}  // namespace test
}  // namespace dev
