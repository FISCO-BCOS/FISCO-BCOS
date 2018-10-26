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
/** @file LedgerInitiailizer.h
 *  @author chaychen
 *  @modify first draft
 *  @date 20181022
 */

#include "LedgerInitiailizer.h"
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/lexical_cast.hpp>

using namespace dev;
using namespace dev::initializer;

void LedgerInitiailizer::initConfig(boost::property_tree::ptree const& _pt)
{
    LOG(INFO) << "LedgerInitiailizer::initConfig";
    m_groupDataDir = _pt.get<std::string>("group.group_data_path", "data/");

    m_ledgerManager = std::make_shared<LedgerManager<FakeLedger>>();
    std::map<GROUP_ID, h512s> groudID2NodeList;

    for (auto it : _pt.get_child("group"))
    {
        if (it.first.find("group_config.") == 0)
        {
            LOG(INFO) << "Load group config, GoupID:" << it.first << ",config:" << it.second.data();

            std::vector<std::string> s;
            try
            {
                boost::split(s, it.first, boost::is_any_of("."), boost::token_compress_on);

                if (s.size() != 2)
                {
                    LOG(ERROR) << "Parse groupID failed:" << it.first.data();
                    continue;
                }

                initSingleGroup(m_ledgerManager, boost::lexical_cast<int>(s[1]), it.second.data(),
                    groudID2NodeList);
            }
            catch (std::exception& e)
            {
                LOG(ERROR) << "Parse group config faield:" << e.what();
                continue;
            }
        }
    }
    m_p2pService->setGroupID2NodeList(groudID2NodeList);
    m_ledgerManager->startAll();
}

void LedgerInitiailizer::initSingleGroup(std::shared_ptr<LedgerManager<FakeLedger>> _ledgerManager,
    GROUP_ID _groupID, std::string const& _path, std::map<GROUP_ID, h512s>& _groudID2NodeList)
{
    _ledgerManager->initSingleLedger(
        m_preCompile, m_p2pService, _groupID, m_keyPair, m_groupDataDir, _path);
    _groudID2NodeList[_groupID] =
        _ledgerManager->getParamByGroupId(_groupID)->mutableConsensusParam().minerList;

    LOG(INFO) << "LedgerInitiailizer::initSingleGroup, groupID:" << std::to_string(_groupID)
              << ",minerList count:" << _groudID2NodeList[_groupID].size()
              << ",consensus status:" << _ledgerManager->consensus(_groupID)->consensusStatus();
    for (auto i : _groudID2NodeList[_groupID])
        LOG(INFO) << "miner:" << toHex(i);
}