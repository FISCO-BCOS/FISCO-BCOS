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
 * @brief : Factory class to create PBFTReq objects
 * @file: PBFTReqFactory.h
 * @author: yujiechen
 * @date: 2018-09-28
 */
#pragma once
#include "GroupPBFTMsgCache.h"
#include "GroupPBFTReqCache.h"
#include <libconsensus/pbft/PBFTReqFactory.h>
namespace dev
{
namespace consensus
{
class GroupPBFTReqFactory : public PBFTReqFactory
{
public:
    virtual ~GroupPBFTReqFactory() {}

    // create PBFTReqCache for group-pbft
    std::shared_ptr<PBFTReqCache> buildPBFTReqCache() override
    {
        std::shared_ptr<PBFTReqCache> reqCache = std::make_shared<GroupPBFTReqCache>();
        return reqCache;
    }

    // create PBFTMsgCache for broadcast
    virtual std::shared_ptr<PBFTMsgCache> buildPBFTMsgCache() override
    {
        std::shared_ptr<PBFTMsgCache> groupPBFTMsgCache = std::make_shared<GroupPBFTMsgCache>();
        return groupPBFTMsgCache;
    }
};
}  // namespace consensus
}  // namespace dev