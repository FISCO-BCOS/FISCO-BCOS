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
/** @file LedgerInitializer.h
 *  @author chaychen
 *  @modify first draft
 *  @date 20181022
 */

#include "LedgerInitializer.h"
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

using namespace dev;
using namespace std;
using namespace dev::initializer;

void LedgerInitializer::initConfig(boost::property_tree::ptree const& _pt)
{
    namespace fs = boost::filesystem;
    INITIALIZER_LOG(DEBUG) << LOG_BADGE("LedgerInitializer") << LOG_DESC("initConfig");
    m_groupDataDir = _pt.get<string>("group.group_data_path", "data/");
    auto groupConfigPath = _pt.get<string>("group.group_config_path", "conf/");
    assert(m_p2pService);
    m_ledgerManager = make_shared<LedgerManager>(m_p2pService, m_keyPair);
    map<GROUP_ID, h512s> groudID2NodeList;
    bool succ = true;
    try
    {
        LOG(INFO) << LOG_BADGE("LedgerInitializer") << LOG_KV("groupConfigPath", groupConfigPath);
        fs::path path(groupConfigPath);
        if (fs::is_directory(path))
        {
            fs::directory_iterator endIter;
            for (fs::directory_iterator iter(path); iter != endIter; iter++)
            {
                if (fs::extension(*iter) == ".genesis")
                {
                    boost::property_tree::ptree pt;
                    boost::property_tree::read_ini(iter->path().string(), pt);
                    auto groupID = pt.get<int>("group.index", 0);
                    if (groupID <= 0)
                    {
                        INITIALIZER_LOG(ERROR)
                            << LOG_BADGE("LedgerInitializer") << LOG_DESC("groupID invalid")
                            << LOG_KV("groupID", groupID)
                            << LOG_KV("configFile", iter->path().string());
                        continue;
                    }
                    succ = initSingleGroup(groupID, iter->path().string(), groudID2NodeList);
                    if (!succ)
                    {
                        INITIALIZER_LOG(ERROR)
                            << LOG_BADGE("LedgerInitializer") << LOG_DESC("initSingleGroup failed")
                            << LOG_KV("configFile", iter->path().string());
                        ERROR_OUTPUT << LOG_BADGE("LedgerInitializer")
                                     << LOG_DESC("initSingleGroup failed")
                                     << LOG_KV("configFile", iter->path().string()) << endl;
                        BOOST_THROW_EXCEPTION(InitLedgerConfigFailed());
                    }
                    LOG(INFO) << LOG_BADGE("LedgerInitializer init group succ")
                              << LOG_KV("groupID", groupID);
                }
            }
        }
        m_p2pService->setGroupID2NodeList(groudID2NodeList);
    }
    catch (exception& e)
    {
        INITIALIZER_LOG(ERROR) << LOG_BADGE("LedgerInitializer")
                               << LOG_DESC("parse group config faield")
                               << LOG_KV("EINFO", boost::diagnostic_information(e));
        ERROR_OUTPUT << LOG_BADGE("LedgerInitializer") << LOG_DESC("parse group config faield")
                     << LOG_KV("EINFO", boost::diagnostic_information(e)) << endl;
        BOOST_THROW_EXCEPTION(e);
    }
    /// stop the node if there is no group
    if (m_ledgerManager->getGrouplList().size() == 0)
    {
        INITIALIZER_LOG(ERROR) << LOG_BADGE("LedgerInitializer")
                               << LOG_DESC("Should init at least one group");
        BOOST_THROW_EXCEPTION(InitLedgerConfigFailed()
                              << errinfo_comment("[#LedgerInitializer]: Should init at least on "
                                                 "group! Please check configuration!"));
    }
}

bool LedgerInitializer::initSingleGroup(
    GROUP_ID _groupID, string const& _path, map<GROUP_ID, h512s>& _groudID2NodeList)
{
    bool succ = m_ledgerManager->initSingleLedger<Ledger>(_groupID, m_groupDataDir, _path);
    if (!succ)
    {
        return succ;
    }
    _groudID2NodeList[_groupID] =
        m_ledgerManager->getParamByGroupId(_groupID)->mutableConsensusParam().sealerList;

    INITIALIZER_LOG(DEBUG) << LOG_BADGE("LedgerInitializer") << LOG_DESC("initSingleGroup")
                           << LOG_KV("groupID", to_string(_groupID));
    return succ;
}
