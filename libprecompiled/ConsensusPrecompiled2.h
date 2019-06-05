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
/** @file ConsensusPrecompiled2.h
 *  @author jimmyshi
 *  @date 20190605
 */
#pragma once
#include "Common.h"
#include "ConsensusPrecompiled.h"
namespace dev
{
namespace precompiled
{
#if 0
contract Sealer {
    function add(string) public constant returns();
    function remove(string) public returns();
}
{
    "b0c8f9dc": "add(string)",
    "80599e4b": "remove(string)"
}
#endif

const char* const NODE_TYPE_OUTSIDER = "outsider";
const char* const NODE_KEY_DISABLENUM = "disable_num";

const int64_t MAX_BLOCK_NUMBER = std::numeric_limits<int64_t>::max();

class ConsensusPrecompiled2 : public dev::blockverifier::Precompiled
{
public:
    typedef std::shared_ptr<ConsensusPrecompiled2> Ptr;
    ConsensusPrecompiled2();
    virtual ~ConsensusPrecompiled2(){};

    virtual bytes call(std::shared_ptr<dev::blockverifier::ExecutiveContext> context,
        bytesConstRef param, Address const& origin = Address());

private:
    void showConsensusTable(std::shared_ptr<dev::blockverifier::ExecutiveContext> context);
    bool checkIsLastSealer(std::shared_ptr<storage::Table> table, std::string const& nodeID);
};

}  // namespace precompiled

}  // namespace dev
