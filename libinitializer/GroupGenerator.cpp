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
/** @file GroupGenerator.cpp
 *  @author jimmyshi
 *  @date 20191209
 */

#include "GroupGenerator.h"
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <fstream>
#include <sstream>

using namespace std;
using namespace dev;
using namespace dev::initializer;

class GenesisTemplate
{
private:
    dev::GROUP_ID m_groupId;
    std::set<std::string> m_sealerList;
    string m_timestamp;

public:
    void setGroupID(dev::GROUP_ID _groupId) { m_groupId = _groupId; }

    void setSealerList(const std::set<std::string>& _sealerList) { m_sealerList = _sealerList; }

    void setTimestamp(const string& _timestamp) { m_timestamp = _timestamp; }

    string newContent() { return ""; }
};


class GroupConfigTempate
{
public:
    string newContent() { return ""; }
};

void GenesisGenerator::generate(const std::string& _confDir, dev::GROUP_ID _groupId,
    const std::string& _timestamp, const std::set<std::string>& _sealerList)
{
    boost::filesystem::path filePath(_confDir + "/" + fileName(_groupId));
    check(filePath.string(), _groupId, _timestamp, _sealerList);

    GenesisTemplate genesisTemplate;
    genesisTemplate.setGroupID(_groupId);
    genesisTemplate.setTimestamp(_timestamp);
    genesisTemplate.setSealerList(_sealerList);

    // genesisTemplate.newContent();
    ofstream outfile;
    outfile.open(filePath.string());
    outfile << genesisTemplate.newContent();

    outfile.close();
}

void GenesisGenerator::check(std::string _filePath, dev::GROUP_ID _groupId,
    const std::string& _timestamp, const std::set<std::string>& _sealerList)
{
    if (boost::filesystem::exists(_filePath))
    {
        BOOST_THROW_EXCEPTION(GenesisExists());
    }

    if (_groupId <= 0)
    {
        BOOST_THROW_EXCEPTION(GroupGeneratorException());
    }

    if (_timestamp.length() == 0)
    {
        BOOST_THROW_EXCEPTION(GroupGeneratorException());
    }

    if (_sealerList.size() == 0)
    {
        BOOST_THROW_EXCEPTION(GroupGeneratorException());
    }
}


void GroupConfigGenerator::generate(const std::string& _confDir, dev::GROUP_ID _groupId)
{
    boost::filesystem::path filePath(_confDir + "/" + fileName(_groupId));
    check(filePath.string(), _groupId);

    GroupConfigTempate groupConfigTemplate;

    // groupConfigTemplate.newContent();
    ofstream outfile;
    outfile.open(filePath.string());
    outfile << groupConfigTemplate.newContent();

    outfile.close();
}

void GroupConfigGenerator::check(std::string _filePath, dev::GROUP_ID _groupId)
{
    if (boost::filesystem::exists(_filePath))
    {
        BOOST_THROW_EXCEPTION(GroupConfigExists());
    }

    if (_groupId <= 0)
    {
        BOOST_THROW_EXCEPTION(GroupGeneratorException());
    }
}


void GroupGenerator::generate(
    dev::GROUP_ID _groupId, const std::string& _timestamp, const std::set<std::string>& _sealerList)
{
    std::string confDir = g_BCOSConfig.confDir();
    GenesisGenerator::generate(confDir, _groupId, _timestamp, _sealerList);
    GroupConfigGenerator::generate(confDir, _groupId);
}

bool GroupGenerator::checkGroupID(int _groupId)
{
    return _groupId > 0 && _groupId <= maxGroupID;
}

bool GroupGenerator::checkTimestamp(const std::string& _timestamp)
{
    try
    {
        int64_t cmp = boost::lexical_cast<int64_t>(_timestamp);
        return _timestamp == std::to_string(cmp);
    }
    catch (...)
    {
        return false;
    }
}

bool GroupGenerator::checkSealerList(const std::set<std::string>& _sealerList)
{
    for (std::string sealer : _sealerList)
    {
        if (!dev::isHex(sealer))
        {
            return false;
        }
        if (sealer.length() != 128)
        {
            return false;
        }
    }
    return true;
}