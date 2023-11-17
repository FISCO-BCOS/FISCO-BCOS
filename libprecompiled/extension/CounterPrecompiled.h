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
/** @file CounterPrecompiled.h
 *  @author yekai
 *  @date 20221118
 */
#pragma once
#include <libprecompiled/Common.h>

namespace dev
{
namespace precompiled
{
class CounterPrecompiled : public dev::precompiled::Precompiled
{
public:
    typedef std::shared_ptr<CounterPrecompiled> Ptr;
    CounterPrecompiled();
    virtual ~CounterPrecompiled(){};

    std::string toString() override;

    PrecompiledExecResult::Ptr call(std::shared_ptr<dev::blockverifier::ExecutiveContext> _context,
        bytesConstRef _param, Address const& _origin = Address(),
        Address const& _sender = Address()) override;
};

}  // namespace precompiled

}  // namespace dev