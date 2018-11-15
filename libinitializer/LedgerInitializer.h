/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file LedgerInitializer.h
 *  @author chaychen
 *  @modify first draft
 *  @date 20181022
 */

#pragma once
#include "Common.h"
#include <libethcore/PrecompiledContract.h>
#include <libledger/Ledger.h>
#include <libledger/LedgerManager.h>
#include <libp2p/P2PInterface.h>

using namespace dev::ledger;

namespace dev
{
namespace initializer
{
class LedgerInitializer : public std::enable_shared_from_this<LedgerInitializer>
{
public:
    typedef std::shared_ptr<LedgerInitializer> Ptr;

    void initConfig(boost::property_tree::ptree const& _pt);

    std::shared_ptr<LedgerManager> ledgerManager() { return m_ledgerManager; }

    void setP2PService(std::shared_ptr<P2PInterface> _p2pService) { m_p2pService = _p2pService; }
    void setKeyPair(KeyPair const& _keyPair) { m_keyPair = _keyPair; }

    ~LedgerInitializer()
    {
        if (m_ledgerManager)
            m_ledgerManager->stopAll();
    }

private:
    bool initSingleGroup(
        GROUP_ID _groupID, std::string const& _path, std::map<GROUP_ID, h512s>& _groudID2NodeList);

    std::shared_ptr<LedgerManager> m_ledgerManager;
    std::shared_ptr<P2PInterface> m_p2pService;
    KeyPair m_keyPair;
    std::string m_groupDataDir;
};

}  // namespace initializer

}  // namespace dev
