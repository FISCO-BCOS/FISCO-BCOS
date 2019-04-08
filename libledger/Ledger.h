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
#include <libethcore/Common.h>
#include <libp2p/P2PInterface.h>
#include <libp2p/Service.h>
#include <boost/algorithm/string.hpp>
#include <boost/property_tree/ptree.hpp>
#define Ledger_LOG(LEVEL) LOG(LEVEL) << "[g:" << std::to_string(m_groupId) << "][LEDGER]"

namespace dev
{
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
     *                  ${data_dir} configurated by the configuration of the ledger, default is
     * "data" (2) if _baseDir is empty, the group data is placed in ./group${_groupId}/${data_dir}
     *
     * @param configFileName: the configuration file path of the ledger, configurated by the
     * main-configuration (1) if configFileName is empty, the configuration path is
     * ./group${_groupId}.ini, (2) if configFileName is not empty, the configuration path is decided
     * by the param ${configFileName}
     */
    Ledger(std::shared_ptr<dev::p2p::P2PInterface> service, dev::GROUP_ID const& _groupId,
        dev::KeyPair const& _keyPair, std::string const& _baseDir,
        std::string const& configFileName)
      : m_service(service),
        m_groupId(_groupId),
        m_keyPair(_keyPair),
        m_configFileName(configFileName)
    {
        m_param = std::make_shared<LedgerParam>();
        std::string prefix = _baseDir + "/group" + std::to_string(_groupId);
        if (_baseDir == "")
            prefix = "./group" + std::to_string(_groupId);
        m_param->setBaseDir(prefix);
        assert(m_service);
        if (m_configFileName == "")
            m_configFileName = "./group." + std::to_string(_groupId) + m_postfixGenesis;

        Ledger_LOG(INFO) << LOG_DESC("LedgerConstructor") << LOG_KV("configPath", m_configFileName)
                         << LOG_KV("baseDir", m_param->baseDir());
        /// The file group.X.genesis is required, otherwise the program terminates.
        /// load genesis config of group
        initConfig(m_configFileName);
        /// The file group.X.ini is available by default.
        /// In this case, the configuration item uses the default value.
        /// load ini config of group for TxPool/Sync modules
        std::string iniConfigFileName = m_configFileName;
        boost::replace_last(iniConfigFileName, m_postfixGenesis, m_postfixIni);

        /// you should invoke initConfig first before invoke initIniConfig
        initIniConfig(iniConfigFileName);
        initMark();
    }

    /// start all modules(sync, consensus)
    void startAll() override
    {
        assert(m_sync && m_sealer);
        Ledger_LOG(INFO) << LOG_DESC("startAll...");
        m_sync->start();
        m_sealer->start();
    }

    /// stop all modules(consensus, sync)
    void stopAll() override
    {
        assert(m_sync && m_sealer);
        Ledger_LOG(INFO) << LOG_DESC("stopAll...") << std::endl;
        m_sealer->stop();
        m_sync->stop();
    }

    virtual ~Ledger(){};

    bool initLedger() override;

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

protected:
    /// load genesis config of group
    void initConfig(std::string const& configPath) override;
    virtual bool initTxPool();
    /// init blockverifier related
    virtual bool initBlockVerifier();
    virtual bool initBlockChain();
    /// create consensus moudle
    virtual bool consensusInitFactory();
    /// init the blockSync
    virtual bool initSync();

    /// make these functions protected for UT
    void initGenesisConfig(boost::property_tree::ptree const& pt);
    void initMark();
    /// load ini config of group
    void initIniConfig(std::string const& iniConfigFileName);
    void initDBConfig(boost::property_tree::ptree const& pt);

private:
    /// create PBFTConsensus
    std::shared_ptr<dev::consensus::Sealer> createPBFTSealer();
    /// create RaftConsensus
    std::shared_ptr<dev::consensus::Sealer> createRaftSealer();
    /// init configurations
    void initCommonConfig(boost::property_tree::ptree const& pt);
    void initTxPoolConfig(boost::property_tree::ptree const& pt);
    void initTxExecuteConfig(boost::property_tree::ptree const& pt);

    void initConsensusConfig(boost::property_tree::ptree const& pt);

    void initConsensusIniConfig(boost::property_tree::ptree const& pt);
    void initSyncConfig(boost::property_tree::ptree const& pt);

    void initTxConfig(boost::property_tree::ptree const& pt);

protected:
    std::shared_ptr<LedgerParamInterface> m_param = nullptr;

    std::shared_ptr<dev::p2p::P2PInterface> m_service = nullptr;
    dev::GROUP_ID m_groupId;
    dev::KeyPair m_keyPair;
    std::string m_configFileName = "config";
    std::string m_postfixGenesis = ".genesis";
    std::string m_postfixIni = ".ini";
    std::shared_ptr<dev::txpool::TxPoolInterface> m_txPool = nullptr;
    std::shared_ptr<dev::blockverifier::BlockVerifierInterface> m_blockVerifier = nullptr;
    std::shared_ptr<dev::blockchain::BlockChainInterface> m_blockChain = nullptr;
    std::shared_ptr<dev::consensus::Sealer> m_sealer = nullptr;
    std::shared_ptr<dev::sync::SyncInterface> m_sync = nullptr;

    std::shared_ptr<dev::ledger::DBInitializer> m_dbInitializer = nullptr;
    ChannelRPCServer::Ptr m_channelRPCServer;
};
}  // namespace ledger
}  // namespace dev
