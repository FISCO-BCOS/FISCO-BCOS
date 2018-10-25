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
 * @file: FakeLedger.h
 * @author: yujiechen
 * @date: 2018-10-23
 */
#pragma once
#include <fisco-bcos/Fake.h>
#include <libledger/Ledger.h>
#include <libledger/LedgerParam.h>
namespace dev
{
namespace ledger
{
class FakeLedger : public Ledger
{
public:
    FakeLedger(std::shared_ptr<dev::p2p::P2PInterface> service, dev::GROUP_ID const& _groupId,
        dev::KeyPair const& _keyPair, std::string const& _baseDir, std::string const& _configFile)
      : Ledger(service, _groupId, _keyPair, _baseDir, _configFile)
    {}
    /// init the ledger(called by initializer)
    void initLedger(
        std::unordered_map<dev::Address, dev::eth::PrecompiledContract> const& preCompile) override
    {
        /// init dbInitializer
        m_dbInitializer = std::make_shared<dev::ledger::DBInitializer>(m_param);
        /// init blockChain
        initBlockChain();
        /// intit blockVerifier
        initBlockVerifier();
        /// init txPool
        initTxPool();
        /// init sync
        initSync();
        /// init consensus
        Ledger::consensusInitFactory();
    }

    /// init blockverifier related
    void initBlockVerifier() override { m_blockVerifier = std::make_shared<FakeBlockVerifier>(); }
    void initBlockChain() override { m_blockChain = std::make_shared<FakeBlockChain>(); }
    /// init the blockSync
    void initSync() override { m_sync = std::make_shared<FakeBlockSync>(); }
};
}  // namespace ledger
}  // namespace dev
