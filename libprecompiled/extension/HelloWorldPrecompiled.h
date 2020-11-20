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
/** @file DagTransferPrecompiled.h
 *  @author octopuswang
 *  @date 20190111
 */
#pragma once
#include <libprecompiled/Common.h>

namespace bcos
{
namespace precompiled
{
class HelloWorldPrecompiled : public bcos::precompiled::Precompiled
{
public:
    typedef std::shared_ptr<HelloWorldPrecompiled> Ptr;
    HelloWorldPrecompiled();
    virtual ~HelloWorldPrecompiled(){};

    std::string toString() override;

    PrecompiledExecResult::Ptr call(std::shared_ptr<bcos::blockverifier::ExecutiveContext> _context,
        bytesConstRef _param, Address const& _origin = Address(),
        Address const& _sender = Address()) override;
};

}  // namespace precompiled

}  // namespace bcos
