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
/** @file WedprPrecompiled.h
 *  @author caryliao
 *  @date 20190917
 */
#pragma once
#include <libblockverifier/ExecutiveContext.h>
#include <libprecompiled/Common.h>

#if 0
contract WedprPrecompiled {

​    function hidden_asset_verify_issued_credit(bytes issue_argument_pb) public;

​    function hidden_asset_verify_fulfilled_credit(bytes fulfill_argument_pb) public;

​    function hidden_asset_verify_transfer_credit(bytes transfer_request_pb) public;

​    function hidden_asset_verify_split_credit(bytes split_request_pb) public;

}
#endif

namespace dev
{
namespace precompiled
{
class WedprPrecompiled : public dev::blockverifier::Precompiled
{
public:
    typedef std::shared_ptr<WedprPrecompiled> Ptr;
    WedprPrecompiled();
    virtual ~WedprPrecompiled(){};

    virtual std::string toString() override;

    virtual bytes call(std::shared_ptr<dev::blockverifier::ExecutiveContext> _context,
        bytesConstRef _param, Address const& _origin = Address()) override;
};

}  // namespace precompiled

}  // namespace dev