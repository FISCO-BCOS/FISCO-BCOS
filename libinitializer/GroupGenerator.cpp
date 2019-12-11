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
 * (c) 2016-2019 fisco-dev contributors.
 */
/**
 * @brief : Generate group files
 * @author: jimmyshi
 * @date: 2019-12-11
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

    string newContent()
    {
        // clang-format off
        std::stringstream content;
        content << "[consensus]\n"
"    ; consensus algorithm type, now support PBFT(consensus_type=pbft) and Raft(consensus_type=raft)\n"
"    consensus_type=pbft\n"
"    ; the max number of transactions of a block\n"
"    max_trans_num=1000\n"
"    ; the node id of consensusers\n"
"";

        int nodeIdx = 0;
        for (string sealer : m_sealerList)
        {
            content << "node." << nodeIdx << "=" << sealer << "\n";
            nodeIdx++;
        }

    content << "[state]\n"
"    ; support mpt/storage\n"
"    type=storage\n"
"[tx]\n"
"    ; transaction gas limit\n"
"    gas_limit=300000000\n"
"[group]\n"
"    id=" << std::to_string(m_groupId) << "\n"
"    timestamp=" << m_timestamp << "\n"
"";

        // clang-format on   
        return content.str();     
    }
};


class GroupConfigTempate {
public:
    string newContent()
    {
        // clang-format off
        std::stringstream content;
        content << "[consensus]\n"
"    ; the ttl for broadcasting pbft message\n"
"    ;ttl=2\n"
"    ; min block generation time(ms), the max block generation time is 1000 ms\n"
"    ;min_block_generation_time=500\n"
"    ;enable_dynamic_block_size=true\n"
"    ;enable_ttl_optimization=true\n"
"    ;enable_prepare_with_txsHash=true\n"
"[storage]\n"
"    ; storage db type, rocksdb / mysql / external / scalable, rocksdb is recommended\n"
"    type=rocksdb\n"
"    ; set true to turn on binary log\n"
"    binary_log=false\n"
"    ; scroll_threshold=scroll_threshold_multiple*1000, only for scalable\n"
"    scroll_threshold_multiple=2\n"
"    ; set fasle to disable CachedStorage\n"
"    cached_storage=true\n"
"    ; max cache memeory, MB\n"
"    max_capacity=32\n"
"    max_forward_block=10\n"
"    ; only for external\n"
"    max_retry=60\n"
"    topic=DB\n"
"    ; only for mysql\n"
"    db_ip=127.0.0.1\n"
"    db_port=3306\n"
"    db_username=\n"
"    db_passwd=\n"
"    db_name=\n"
"[tx_pool]\n"
"    limit=150000\n"
"[tx_execute]\n"
"    enable_parallel=true\n"
"[sync]\n"
"    idle_wait_ms=200\n"
"    ; send block status and transaction by tree-topology\n"
"    ; only supported when use pbft\n"
"    sync_by_tree=true\n"
"    ; must between 1000 to 3000\n"
"    ; only enabled when sync_by_tree is true\n"
"    gossip_interval_ms=1000\n"
"    gossip_peers_number=3\n"
"";

        // clang-format on        
        return content.str();
    }
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
    int _groupId, const std::string& _timestamp, const std::set<std::string>& _sealerList)
{
    if (!checkGroupID(_groupId))
    {
        BOOST_THROW_EXCEPTION(GroupGeneratorException());
    }

    if (!checkTimestamp(_timestamp))
    {
        BOOST_THROW_EXCEPTION(GroupGeneratorException());
    }

    if (!checkSealerList(_sealerList))
    {
        BOOST_THROW_EXCEPTION(GroupGeneratorException());
    }

    std::string confDir = g_BCOSConfig.confDir();
    GenesisGenerator::generate(confDir, GROUP_ID(_groupId), _timestamp, _sealerList);
    GroupConfigGenerator::generate(confDir, GROUP_ID(_groupId));
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
        if (sealer.compare(0, 2, "0x") == 0) 
        {
            return false;
        }
    }
    return true;
}