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
    INITIALIZER_LOG(DEBUG) << "[#LedgerInitiailizer::initConfig]";
    m_groupDataDir = _pt.get<std::string>("group.group_data_path", "data/");
    assert(m_p2pService);
    /// TODO: modify FakeLedger to the real Ledger after all modules ready
    m_ledgerManager = std::make_shared<LedgerManager>(m_p2pService, m_keyPair);
    std::map<GROUP_ID, h512s> groudID2NodeList;
    bool succ = true;
    for (auto it : _pt.get_child("group"))
    {
        if (it.first.find("group_config.") == 0)
        {
            INITIALIZER_LOG(TRACE)
                << "[#LedgerInitiailizer::initConfig] load group config: [groupID/config]: "
                << it.first << "/" << it.second.data();

            std::vector<std::string> s;
            try
            {
                boost::split(s, it.first, boost::is_any_of("."), boost::token_compress_on);

                if (s.size() != 2)
                {
                    INITIALIZER_LOG(ERROR)
                        << "[#LedgerInitiailizer::initConfig] parse groupID failed: [data]: "
                        << it.first.data();
                    BOOST_THROW_EXCEPTION(
                        InvalidConfig() << errinfo_comment(
                            "[#LedgerInitiailizer::initConfig] parse groupID failed!"));
                    exit(1);
                }

                succ = initSingleGroup(
                    boost::lexical_cast<int>(s[1]), it.second.data(), groudID2NodeList);
                if (!succ)
                {
                    INITIALIZER_LOG(ERROR)
                        << "[#LedgerInitiailizer::initConfig] initSingleGroup for "
                        << boost::lexical_cast<int>(s[1]) << "failed" << std::endl;
                    BOOST_THROW_EXCEPTION(
                        InvalidConfig() << errinfo_comment(
                            "[#LedgerInitiailizer::initConfig] initSingleGroup for " + s[1] +
                            " failed!"));
                    exit(1);
                }
            }
            catch (std::exception& e)
            {
                SESSION_LOG(ERROR)
                    << "[#LedgerInitiailizer::initConfig] parse group config faield: [EINFO]: "
                    << e.what();
                BOOST_THROW_EXCEPTION(
                    InvalidConfig() << errinfo_comment("[#LedgerInitiailizer::initConfig] "
                                                       "initSingleGroup failed: [EINFO]: " +
                                                       boost::diagnostic_information(e)));
                exit(1);
            }
        }
    }
    m_p2pService->setGroupID2NodeList(groudID2NodeList);
    m_ledgerManager->startAll();
}

bool LedgerInitiailizer::initSingleGroup(
    GROUP_ID _groupID, std::string const& _path, std::map<GROUP_ID, h512s>& _groudID2NodeList)
{
    bool succ = m_ledgerManager->initSingleLedger<Ledger>(_groupID, m_groupDataDir, _path);
    _groudID2NodeList[_groupID] =
        m_ledgerManager->getParamByGroupId(_groupID)->mutableConsensusParam().minerList;

    INITIALIZER_LOG(DEBUG) << "[#initSingleGroup] [groupID/count/status]: "
                           << std::to_string(_groupID) << "/" << _groudID2NodeList[_groupID].size()
                           << "/" << m_ledgerManager->consensus(_groupID)->consensusStatus();
    for (auto i : _groudID2NodeList[_groupID])
        INITIALIZER_LOG(TRACE) << "miner:" << toHex(i);
    return succ;
}
