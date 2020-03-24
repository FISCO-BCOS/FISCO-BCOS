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
/** @file GroupSigPrecompiled.h
 *  @author shawnhe
 *  @date 20190821
 */

#pragma once
#include <libblockverifier/ExecutiveContext.h>
#include <libprecompiled/Common.h>

namespace dev
{
namespace precompiled
{
#if 0
contract GroupSig
{
    function groupSigVerify(string signature, string message, string gpkInfo, string paramInfo) public constant returns(bool);
}
#endif

class GroupSigPrecompiled : public dev::precompiled::Precompiled
{
public:
    typedef std::shared_ptr<GroupSigPrecompiled> Ptr;
    GroupSigPrecompiled();
    virtual ~GroupSigPrecompiled(){};

    bytes call(std::shared_ptr<dev::blockverifier::ExecutiveContext> context, bytesConstRef param,
        Address const& origin = Address(), Address const& sender = Address()) override;
};

}  // namespace precompiled
}  // namespace dev
