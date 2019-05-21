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
 * @brief : implementation of Ledger manager
 * @file: LedgerManager.cpp
 * @author: yujiechen
 * @date: 2019-5-21
 */
#include "LedgerManager.h"
#include <algorithm>

namespace dev
{
namespace ledger
{
std::set<dev::GROUP_ID> LedgerManager::getGroupListForRpc() const
{
    std::set<dev::GROUP_ID> groupList;
    for (auto const& ledger : m_ledgerMap)
    {
        // check sealer list
        auto sealerList = ledger.second->blockChain()->sealerList();
        auto it_sealer = find(sealerList.begin(), sealerList.end(), ledger.second->keyPair().pub());
        if (it_sealer != sealerList.end())
        {
            groupList.insert(ledger.second->groupId());
            continue;
        }
        /// check observer list
        auto observerList = ledger.second->blockChain()->observerList();
        auto it_observer =
            find(observerList.begin(), observerList.end(), ledger.second->keyPair().pub());
        if (it_observer != observerList.end())
        {
            groupList.insert(ledger.second->groupId());
        }
    }
    return groupList;
}
}  // namespace ledger
}  // namespace dev
