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
#include <json/json.h>
#include <libblockverifier/BlockVerifierInterface.h>
#include <libblockverifier/ExecutiveContext.h>
#include <libdevcore/FixedHash.h>
#include <libethcore/BlockFactory.h>

#define SEAL_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("CONSENSUS") << LOG_BADGE("SEALER")
#define ENGINE_LOG(LEVEL) LOG(LEVEL) << LOG_BADGE("CONSENSUS") << LOG_BADGE("ConsensusEngine")
namespace dev
{
namespace consensus
{
typedef uint16_t IDXTYPE;
typedef int64_t VIEWTYPE;
static const IDXTYPE MAXIDX = UINT16_MAX;
static const VIEWTYPE MAXVIEW = INT64_MAX;
static const uint8_t MAXTTL = 2;
DEV_SIMPLE_EXCEPTION(DisabledFutureTime);
DEV_SIMPLE_EXCEPTION(OverThresTransNum);
DEV_SIMPLE_EXCEPTION(InvalidBlockHeight);
DEV_SIMPLE_EXCEPTION(ExistedBlock);
DEV_SIMPLE_EXCEPTION(ParentNoneExist);
DEV_SIMPLE_EXCEPTION(WrongParentHash);
DEV_SIMPLE_EXCEPTION(BlockSealerListWrong);
DEV_SIMPLE_EXCEPTION(InvalidBlockTimeStamp);

enum NodeAccountType
{
    ObserverAccount = 0,
    SealerAccount
};

class Sealing
{
public:
    Sealing() {}
    Sealing(dev::eth::BlockFactory::Ptr _blockFactory) { setBlockFactory(_blockFactory); }
    void setBlockFactory(dev::eth::BlockFactory::Ptr _blockFactory)
    {
        m_blockFactory = _blockFactory;
        block = m_blockFactory->createBlock();
    }

    std::shared_ptr<dev::eth::Block> block;
    /// hash set for filter fetched transactions
    h256Hash m_transactionSet;
    dev::blockverifier::ExecutiveContext::Ptr p_execContext;
    dev::eth::BlockFactory::Ptr m_blockFactory;
};

}  // namespace consensus
}  // namespace dev
