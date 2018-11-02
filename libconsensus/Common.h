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
 * @brief Common functions and types of Consensus module
 * @author: yujiechen
 * @date: 2018-09-21
 */
#pragma once
#include <libblockverifier/BlockVerifierInterface.h>
#include <libblockverifier/ExecutiveContext.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/easylog.h>
#include <libethcore/Block.h>
#define SEAL_LOG(LEVEL) \
    LOG(LEVEL) << "[#Seal] [PROTOCOL: " << m_consensusEngine->protocolId() << "] "
#define ENGINE_LOG(LEVEL) LOG(LEVEL) << "[#ConsensusEngine] [PROTOCOL: " << m_protocolId << "] "
namespace dev
{
namespace consensus
{
DEV_SIMPLE_EXCEPTION(DisabledFutureTime);
DEV_SIMPLE_EXCEPTION(InvalidBlockHeight);
DEV_SIMPLE_EXCEPTION(ExistedBlock);
DEV_SIMPLE_EXCEPTION(ParentNoneExist);
DEV_SIMPLE_EXCEPTION(BlockMinerListWrong);

enum NodeAccountType
{
    ObserverAccount = 0,
    MinerAccount
};
struct Sealing
{
    dev::eth::Block block;
    /// hash set for filter fetched transactions
    h256Hash m_transactionSet;
    dev::blockverifier::ExecutiveContext::Ptr p_execContext;
};

}  // namespace consensus
}  // namespace dev
