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
 * @brief : Generate transaction DAG with level for parallel execution
 * @author: jimmyshi
 * @date: 2019-1-16
 */

#pragma once
#include "TxDAG.h"
#include <libethcore/Block.h>
#include <libethcore/Transaction.h>
#include <memory>
#include <queue>
#include <vector>


namespace dev
{
namespace blockverifier
{
using IDs = std::vector<ID>;
using IDLevels = std::vector<std::shared_ptr<IDs>>;
using LevelID = int;
static const LevelID INVALID_LevelID = LevelID(-1);  // INVALID_LevelID must smaller than 0

class TxLevelDAG : public TxDAGFace
{
public:
    TxLevelDAG() {}
    ~TxLevelDAG() {}

    // Generate DAG according with given transactions
    void init(ExecutiveContext::Ptr _ctx, dev::eth::Transactions const& _txs);

    // Set transaction execution function
    void setTxExecuteFunc(ExecuteTxFunc const& _f);

    // Called by thread
    // Has the DAG reach the end?
    bool hasFinished() override { return false; }

    // get a DAG level containing tx id, that can be parallel execute
    // return nullptr if DAG reach the end
    std::shared_ptr<IDs> getDAGLevel();

    // execute a given tx by a tx id
    int executeByID(ID _id);

    // not used at all
    int executeUnit() override { return 0; };

private:
    ExecuteTxFunc f_executeTx;
    std::shared_ptr<dev::eth::Transactions const> m_txs;

    IDLevels m_levels;
    LevelID m_currentExecuteLevel = 0;

private:
    void addToLevel(ID _id, LevelID _level);
};

template <typename T>
class LevelCriticalField
{
public:
    // return max level ID of a given critical field
    LevelID get(T const& _c)
    {
        auto it = m_criticals.find(_c);
        if (it == m_criticals.end())
            return INVALID_LevelID;
        return it->second;
    }

    // update max level ID to a critical field
    void update(T const& _c, LevelID _levelId)
    {
        auto it = m_criticals.find(_c);
        if (it == m_criticals.end())
            m_criticals[_c] = _levelId;
        else
            it->second = std::max(it->second, _levelId);
    }

private:
    std::map<T, LevelID> m_criticals;
};

}  // namespace blockverifier
}  // namespace dev