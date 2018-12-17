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
/** @file ConsensusPrecompiled.h
 *  @author chaychen
 *  @date 20181211
 */
#pragma once
#include "libblockverifier/ExecutiveContext.h"
#include "libstorage/CRUDPrecompiled.h"
namespace dev
{
namespace blockverifier
{
#if 0
contract Miner {
    function add(string) public constant returns();
    function cancel(string) public returns();
}
{
    "b0c8f9dc": "add(string)",
    "80599e4b": "remove(string)"
}
#endif

/// \brief Sign of the miner is valid or not
const char* const NODE_TYPE = "type";
const char* const NODE_TYPE_MINER = "miner";
const char* const NODE_TYPE_OBSERVER = "observer";
const char* const NODE_KEY_NODEID = "node_id";
const char* const NODE_KEY_ENABLENUM = "enable_num";
const char* const PRI_COLUMN = "name";
const char* const PRI_KEY = "node";

class ConsensusPrecompiled : public CRUDPrecompiled
{
public:
    typedef std::shared_ptr<ConsensusPrecompiled> Ptr;

    virtual ~ConsensusPrecompiled(){};

    virtual bytes call(ExecutiveContext::Ptr context, bytesConstRef param);

private:
    void showConsensusTable(ExecutiveContext::Ptr context);
    bool checkIsLastMiner(storage::Table::Ptr table, std::string const& nodeID);
};

}  // namespace blockverifier

}  // namespace dev
