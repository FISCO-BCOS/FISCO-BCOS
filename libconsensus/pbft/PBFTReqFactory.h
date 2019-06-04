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

/**
 * @brief : Factory class to create PBFTReq objects
 * @file: PBFTReqFactory.h
 * @author: yujiechen
 * @date: 2018-09-28
 */
#pragma once
#include "Common.h"
#include "PBFTMsgCache.h"
#include "PBFTReqCache.h"

namespace dev
{
namespace consensus
{
class PBFTReqFactoryInterface
{
public:
    virtual ~PBFTReqFactoryInterface(){};
    // create PBFTReqCache
    virtual std::shared_ptr<PBFTReqCache> buildPBFTReqCache() = 0;
    // create PBFTMsgCache for broadcast
    virtual std::shared_ptr<PBFTMsgCache> buildPBFTMsgCache() = 0;
};

/// this is used for PBFT consensus algorithem
class PBFTReqFactory : public PBFTReqFactoryInterface
{
public:
    virtual ~PBFTReqFactory(){};
    std::shared_ptr<PBFTReqCache> buildPBFTReqCache() override
    {
        std::shared_ptr<PBFTReqCache> reqCache = std::make_shared<PBFTReqCache>();
        return reqCache;
    }
    std::shared_ptr<PBFTMsgCache> buildPBFTMsgCache() override
    {
        std::shared_ptr<PBFTMsgCache> pbftMsgCache = std::make_shared<PBFTMsgCache>();
        return pbftMsgCache;
    }
};
}  // namespace consensus
}  // namespace dev