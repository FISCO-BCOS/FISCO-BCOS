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
/** @file ConsensusPrecompiled.h
 *  @author chaychen
 *  @date 20180921
 */
#pragma once
#include "Common.h"
namespace dev
{
namespace precompiled
{
/// \brief Sign of the sealer is valid or not
const char* const NODE_TYPE = "type";
const char* const NODE_TYPE_SEALER = "sealer";
const char* const NODE_TYPE_OBSERVER = "observer";
const char* const NODE_KEY_NODEID = "node_id";
const char* const NODE_KEY_ENABLENUM = "enable_num";
const char* const PRI_COLUMN = "name";
const char* const PRI_KEY = "node";

class ConsensusPrecompiled : public dev::precompiled::Precompiled
{
public:
    typedef std::shared_ptr<ConsensusPrecompiled> Ptr;
    ConsensusPrecompiled();
    virtual ~ConsensusPrecompiled(){};

    PrecompiledExecResult::Ptr call(std::shared_ptr<dev::blockverifier::ExecutiveContext> context,
        bytesConstRef param, Address const& origin = Address(),
        Address const& _sender = Address()) override;

private:
    void showConsensusTable(std::shared_ptr<dev::blockverifier::ExecutiveContext> context);
    bool checkIsLastSealer(std::shared_ptr<storage::Table> table, std::string const& nodeID);
};

}  // namespace precompiled

}  // namespace dev
