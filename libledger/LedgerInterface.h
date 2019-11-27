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
 * @brief : external interface of libledger
 * @file: LedgerInterface.h
 * @author: yujiechen
 * @date: 2018-10-23
 */
#pragma once
#include "LedgerParamInterface.h"
#include <libblockchain/BlockChainInterface.h>
#include <libblockverifier/BlockVerifierInterface.h>
#include <libchannelserver/ChannelRPCServer.h>
#include <libconsensus/ConsensusInterface.h>
#include <libethcore/Protocol.h>
#include <libsync/SyncInterface.h>
#include <libtxpool/TxPoolInterface.h>
#include <memory>
namespace dev
{
namespace event
{
class EventLogFilterManager;
}

namespace ledger
{
class LedgerInterface
{
public:
    LedgerInterface(dev::KeyPair const& keyPair) : m_keyPair(keyPair) {}

    virtual ~LedgerInterface(){};
    /// init the ledger(called by initializer)
    virtual bool initLedger(std::shared_ptr<LedgerParamInterface> _ledgerParams) = 0;

    virtual std::shared_ptr<dev::txpool::TxPoolInterface> txPool() const = 0;
    virtual std::shared_ptr<dev::blockverifier::BlockVerifierInterface> blockVerifier() const = 0;
    virtual std::shared_ptr<dev::blockchain::BlockChainInterface> blockChain() const = 0;
    virtual std::shared_ptr<dev::consensus::ConsensusInterface> consensus() const = 0;
    virtual std::shared_ptr<dev::sync::SyncInterface> sync() const = 0;
    virtual dev::GROUP_ID const& groupId() const = 0;
    virtual std::shared_ptr<LedgerParamInterface> getParam() const = 0;
    virtual void startAll() = 0;
    virtual void stopAll() = 0;
    virtual dev::KeyPair const& keyPair() const { return m_keyPair; };
    virtual void setChannelRPCServer(ChannelRPCServer::Ptr channelRPCServer)
    {
        (void)channelRPCServer;
    };
    virtual std::shared_ptr<dev::event::EventLogFilterManager> getEventLogFilterManager() = 0;

protected:
    dev::KeyPair m_keyPair;
};
}  // namespace ledger
}  // namespace dev
