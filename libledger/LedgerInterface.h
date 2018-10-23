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
#include <initializer/ParamInterface.h>
#include <libblockchain/BlockChainInterface.h>
#include <libblocksync/SyncInterface.h>
#include <libblockverifier/BlockVerifierInterface.h>
#include <libconsensus/Consensus.h>
#include <libconsensus/ConsensusInterface.h>
#include <libtxpool/TxPoolInterface.h>
#include <memory>
namespace dev
{
namespace ledger
{
class LedgerInterface
{
public:
    LedgerInterface() = default;
    virtual ~LedgerInterface(){};
    /// init the ledger(called by initializer)
    virtual bool initLedger(std::shared_ptr<dev::initializer::LedgerParamInterface> param) = 0;
    virtual std::shared_ptr<dev::txpool::TxPoolInterface> txPool() const = 0;
    virtual std::shared_ptr<dev::blockverifier::BlockVerifierInterface> blockVerifier() const = 0;
    virtual std::shared_ptr<dev::blockchain::BlockChainInterface> blockChain() const = 0;
    virtual std::shared_ptr<dev::consensus::ConsensusInterface> consensus() const = 0;
    virtual std::shared_ptr<dev::sync::SyncInterface> sync() const = 0;
};
}  // namespace ledger
}  // namespace dev