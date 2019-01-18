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

#include "TxLevelDAG.h"
#include "Common.h"
#include <map>

using namespace std;
using namespace dev;
using namespace dev::eth;
using namespace dev::blockverifier;

// Generate DAG according with given transactions
void TxLevelDAG::init(ExecutiveContext::Ptr _ctx, Transactions const& _txs)
{
    m_txs = make_shared<Transactions const>(_txs);

    LevelCriticalField<string> latestCriticals;

    for (ID id = 0; id < _txs.size(); id++)
    {
        auto& tx = _txs[id];

        // Is para transaction? //XXX Serial transaction has all criticals it will seperate DAG
        auto p = _ctx->getPrecompiled(tx.receiveAddress());
        if (!p || !p->isDagPrecompiled())
        {
            continue;
        }

        // Get critical field
        vector<string> criticals = p->getDagTag(ref(tx.data()));

        // Add tx id into maxlevel + 1
        LevelID maxLevelId = INVALID_LevelID;
        for (string const& c : criticals)
        {
            maxLevelId = max(maxLevelId, latestCriticals.get(c));
        }
        LevelID currTxLevel = maxLevelId + 1;
        addToLevel(id, currTxLevel);

        // update critical field
        for (string const& c : criticals)
        {
            latestCriticals.update(c, currTxLevel);
        }
    }

    size_t maxLevelSize = 0;
    for (auto& level : m_levels)
        maxLevelSize = max(maxLevelSize, level->size());
    PARA_LOG(DEBUG) << LOG_BADGE("LevelDAG") << LOG_DESC("level info")
                    << LOG_KV("levels", m_levels.size()) << LOG_KV("maxLevelSize", maxLevelSize);
}

// Set transaction execution function
void TxLevelDAG::setTxExecuteFunc(ExecuteTxFunc const& _f)
{
    f_executeTx = _f;
}

int TxLevelDAG::executeByID(ID _id)
{
    f_executeTx((*m_txs)[_id], _id);
    return 1;
}

std::shared_ptr<IDs> TxLevelDAG::getDAGLevel()
{
    if (m_currentExecuteLevel >= LevelID(m_levels.size()))
        return nullptr;
    m_currentExecuteLevel++;
    return m_levels[m_currentExecuteLevel - 1];
}


void TxLevelDAG::addToLevel(ID _id, LevelID _level)
{
    if (_level < 0)
        return;
    // add new level if not exist
    if (m_levels.size() <= (size_t)_level)
    {
        size_t needNewLevels = (size_t)_level - m_levels.size() + 1;
        for (size_t i = 0; i < needNewLevels; i++)
            m_levels.emplace_back(make_shared<IDs>());
    }

    m_levels[_level]->emplace_back(_id);
}