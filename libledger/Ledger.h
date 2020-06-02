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
 * @brief : implementation of Ledger
 * @file: Ledger.h
 * @author: yujiechen
 * @date: 2018-10-23
 */
#pragma once
#include "DBInitializer.h"
#include "LedgerInterface.h"
#include "LedgerParam.h"
#include "LedgerParamInterface.h"
#include <libconsensus/Sealer.h>
#include <libdevcore/Exceptions.h>
#include <libdevcrypto/Common.h>
#include <libethcore/BlockFactory.h>
#include <libethcore/Common.h>
#include <libeventfilter/EventLogFilterManager.h>
#include <libp2p/P2PInterface.h>
#include <libp2p/Service.h>
#include <libstat/NetworkStatHandler.h>
#include <boost/algorithm/string.hpp>
#include <boost/property_tree/ptree.hpp>

#define Ledger_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("LEDGER")

namespace dev
{
namespace event
{
class EventLogFilterManager;
}

namespace ledger
{
class Ledger : public LedgerInterface
{
public:
    /**
     * @brief: init a single ledger with specified params
     * @param service : p2p handler
     * @param _groupId : group id of the ledger belongs to
     * @param _keyPair : keyPair used to init the consensus Sealer
     * @param _baseDir: baseDir used to place the data of the ledger
     *                  (1) if _baseDir not empty, the group data is placed in
     * ${_baseDir}/group${_groupId}/${data_dir},
     *                  ${data_dir} configured by the configuration of the ledger, default is
     * "data" (2) if _baseDir is empty, the group data is placed in ./group${_groupId}/${data_dir}
     *
     * @param configFileName: the configuration file path of the ledger, configured by the
     * main-configuration (1) if configFileName is empty, the configuration path is
     * ./group${_groupId}.ini, (2) if configFileName is not empty, the configuration path is decided
     * by the param ${configFileName}
     */
    Ledger(std::shared_ptr<dev::p2p::P2PInterface> service, dev::GROUP_ID const& _groupId,
        dev::KeyPair const& _keyPair)
      : LedgerInterface(_keyPair), m_service(service), m_groupId(_groupId)
    {
        assert(m_service);
    }

    /// start all modules(sync, consensus)
    void startAll() override
    {
        assert(m_sync && m_sealer);
        /// tag this scope with GroupId
        BOOST_LOG_SCOPED_THREAD_ATTR(
            "GroupId", boost::log::attributes::constant<std::string>(std::to_string(m_groupId)));
        Ledger_LOG(INFO) << LOG_DESC("startAll...");

        m_txPool->registerSyncStatusChecker([this]() {
            try
            {
                // Refuse transaction if far syncing
                if (m_sync->blockNumberFarBehind())
                {
                    BOOST_THROW_EXCEPTION(
                        TransactionRefused() << errinfo_comment("ImportResult::NodeIsSyncing"));
                }
            }
            catch (std::exception const& _e)
            {
                return false;
            }
            return true;
        });
        m_txPool->start();
        m_sync->start();
        m_sealer->start();
        m_eventLogFilterManger->start();
    }

    /// stop all modules(consensus, sync)
    void stopAll() override
    {
        assert(m_sync && m_sealer);
        Ledger_LOG(INFO) << LOG_DESC("stop sealer") << LOG_KV("groupID", groupId());
        m_sealer->stop();
        Ledger_LOG(INFO) << LOG_DESC("sealer stopped. stop sync") << LOG_KV("groupID", groupId());
        m_sync->stop();
        Ledger_LOG(INFO) << LOG_DESC("sync stopped. stop event filter manager")
                         << LOG_KV("groupID", groupId());
        m_eventLogFilterManger->stop();
        Ledger_LOG(INFO) << LOG_DESC("event filter manager stopped. stop tx pool")
                         << LOG_KV("groupID", groupId());
        m_txPool->stop();
        if (m_service)
        {
            Ledger_LOG(INFO) << LOG_DESC("removeNetworkStatHandlerByGroupID for service")
                             << LOG_KV("groupID", groupId());
            m_service->removeNetworkStatHandlerByGroupID(groupId());
        }
        if (m_stopped)
        {
            return;
        }
        if (m_channelRPCServer && m_channelRPCServer->networkStatHandler())
        {
            Ledger_LOG(INFO) << LOG_DESC("removeNetworkStatHandlerByGroupID for channelRPCServer")
                             << LOG_KV("groupID", groupId());
            m_channelRPCServer->networkStatHandler()->removeGroupP2PStatHandler(groupId());
            m_stopped.store(true);
        }
    }

    virtual ~Ledger(){};

    bool initLedger(std::shared_ptr<LedgerParamInterface> _ledgerParams) override;

    std::shared_ptr<dev::txpool::TxPoolInterface> txPool() const override { return m_txPool; }
    std::shared_ptr<dev::blockverifier::BlockVerifierInterface> blockVerifier() const override
    {
        return m_blockVerifier;
    }
    std::shared_ptr<dev::blockchain::BlockChainInterface> blockChain() const override
    {
        return m_blockChain;
    }
    virtual std::shared_ptr<dev::consensus::ConsensusInterface> consensus() const override
    {
        if (m_sealer)
        {
            return m_sealer->consensusEngine();
        }
        return nullptr;
    }
    std::shared_ptr<dev::sync::SyncInterface> sync() const override { return m_sync; }
    virtual dev::GROUP_ID const& groupId() const override { return m_groupId; }
    std::shared_ptr<LedgerParamInterface> getParam() const override { return m_param; }

    virtual void setChannelRPCServer(ChannelRPCServer::Ptr channelRPCServer) override
    {
        m_channelRPCServer = channelRPCServer;
    }

    std::shared_ptr<dev::event::EventLogFilterManager> getEventLogFilterManager() override
    {
        return m_eventLogFilterManger;
    }

protected:
    virtual bool initTxPool();
    /// init blockverifier related
    virtual bool initBlockVerifier();
    virtual bool initBlockChain(GenesisBlockParam& _genesisParam);
    /// create consensus moudle
    virtual bool consensusInitFactory();
    /// init the blockSync
    virtual bool initSync();
    // init EventLogFilterManager
    virtual bool initEventLogFilterManager();
    // init statHandler
    virtual void initNetworkStatHandler();

    void initGenesisMark(GenesisBlockParam& genesisParam);
    /// load ini config of group
    void initIniConfig(std::string const& iniConfigFileName);
    void initDBConfig(boost::property_tree::ptree const& pt);

    dev::consensus::ConsensusInterface::Ptr createConsensusEngine(
        dev::PROTOCOL_ID const& _protocolId);
    dev::eth::BlockFactory::Ptr createBlockFactory();
    void initPBFTEngine(dev::consensus::Sealer::Ptr _sealer);
    void initRotatingPBFTEngine(dev::consensus::Sealer::Ptr _sealer);

private:
    /// create PBFTConsensus
    std::shared_ptr<dev::consensus::Sealer> createPBFTSealer();
    /// create RaftConsensus
    std::shared_ptr<dev::consensus::Sealer> createRaftSealer();

    bool isRotatingPBFTEnabled();

protected:
    std::shared_ptr<LedgerParamInterface> m_param = nullptr;

    std::shared_ptr<dev::p2p::P2PInterface> m_service = nullptr;
    dev::GROUP_ID m_groupId;
    std::shared_ptr<dev::txpool::TxPoolInterface> m_txPool = nullptr;
    std::shared_ptr<dev::blockverifier::BlockVerifierInterface> m_blockVerifier = nullptr;
    std::shared_ptr<dev::blockchain::BlockChainInterface> m_blockChain = nullptr;
    std::shared_ptr<dev::consensus::Sealer> m_sealer = nullptr;
    std::shared_ptr<dev::sync::SyncInterface> m_sync = nullptr;
    std::shared_ptr<dev::event::EventLogFilterManager> m_eventLogFilterManger = nullptr;
    // for network statistic
    std::shared_ptr<dev::stat::NetworkStatHandler> m_networkStatHandler = nullptr;

    std::shared_ptr<dev::ledger::DBInitializer> m_dbInitializer = nullptr;
    ChannelRPCServer::Ptr m_channelRPCServer;
    std::atomic_bool m_stopped = {false};

    dev::eth::Handler<int64_t> m_handler;
};
}  // namespace ledger
}  // namespace dev
