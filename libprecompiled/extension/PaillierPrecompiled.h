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
/** @file PaillierPrecompiled.h
 *  @author shawnhe
 *  @date 20190808
 */

#pragma once
#include <libblockverifier/ExecutiveContext.h>
#include <libprecompiled/Common.h>

class CallPaillier;
namespace dev
{
namespace precompiled
{
#if 0
contract Paillier 
{
    function paillierAdd(string cipher1, string cipher2) public constant returns(string);
}
#endif

class PaillierPrecompiled : public dev::precompiled::Precompiled
{
public:
    typedef std::shared_ptr<PaillierPrecompiled> Ptr;
    PaillierPrecompiled();
    virtual ~PaillierPrecompiled(){};

    bytes call(std::shared_ptr<dev::blockverifier::ExecutiveContext> context, bytesConstRef param,
        Address const& origin = Address(), Address const& sender = Address()) override;

private:
    std::shared_ptr<CallPaillier> m_callPaillier;
};

}  // namespace precompiled
}  // namespace dev
