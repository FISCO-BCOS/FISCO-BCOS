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
/** @file Precompiled.h
 *  @author mingzhenliu
 *  @date 20180921
 */
#pragma once

#include <libdevcore/FixedHash.h>
#include <memory>

namespace dev
{
namespace blockverifier
{
class ExecutiveContext;

struct BlockInfo
{
    h256 hash;
    u256 number;
};

class Precompiled : public std::enable_shared_from_this<Precompiled>
{
public:
    typedef std::shared_ptr<Precompiled> Ptr;

    virtual ~Precompiled(){};


    virtual std::string toString(std::shared_ptr<ExecutiveContext>) { return ""; }

    virtual bytes call(std::shared_ptr<ExecutiveContext> context, bytesConstRef param) = 0;

    virtual uint32_t getParamFunc(bytesConstRef param)
    {
        auto funcBytes = param.cropped(0, 4);
        uint32_t func = *((uint32_t*)(funcBytes.data()));

        return ((func & 0x000000FF) << 24) | ((func & 0x0000FF00) << 8) |
               ((func & 0x00FF0000) >> 8) | ((func & 0xFF000000) >> 24);
    }

    virtual bytesConstRef getParamData(bytesConstRef param) { return param.cropped(4); }
};

}  // namespace blockverifier

}  // namespace dev
