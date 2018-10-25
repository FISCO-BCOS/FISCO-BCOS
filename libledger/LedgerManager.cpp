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
 * @date: 2018-10-23
 */
#include "LedgerManager.h"
#include <libdevcore/easylog.h>

namespace dev
{
namespace ledger
{
bool LedgerManager::initSingleLedger(std::shared_ptr<dev::p2p::P2PInterface> service,
    dev::eth::GroupID const& _groupId, dev::KeyPair const& _keyPair, std::string const& _baseDir)
{
    if (m_ledgerMap.count(_groupId) == 0)
    {
        std::shared_ptr<LedgerInterface> ledger =
            std::make_shared<Ledger>(service, _groupId, _keyPair, _baseDir);
        m_ledgerMap.insert(std::make_pair(_groupId, ledger));
        return true;
    }
    else
    {
        LOG(WARNING) << "Group " << _groupId << " has been created already";
        return false;
    }
}

}  // namespace ledger
}  // namespace dev