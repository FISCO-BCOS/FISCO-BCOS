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
/** @file CNSPrecompiled.h
 *  @author chaychen
 *  @date 20181119
 */
#pragma once
#include "Common.h"
#include <libblockverifier/ExecutiveContext.h>

namespace dev
{
namespace storage
{
class Table;
}

const std::string SYS_CNS_FIELD_NAME = "name";
const std::string SYS_CNS_FIELD_VERSION = "version";
const std::string SYS_CNS_FIELD_ADDRESS = "address";
const std::string SYS_CNS_FIELD_ABI = "abi";

namespace precompiled
{
class CNSPrecompiled : public dev::blockverifier::Precompiled
{
public:
    typedef std::shared_ptr<CNSPrecompiled> Ptr;
    CNSPrecompiled();
    virtual ~CNSPrecompiled(){};

    std::string toString(dev::blockverifier::ExecutiveContext::Ptr) override;

    bytes call(dev::blockverifier::ExecutiveContext::Ptr context, bytesConstRef param,
        Address const& origin = Address()) override;

protected:
    std::shared_ptr<storage::Table> openTable(
        dev::blockverifier::ExecutiveContext::Ptr context, const std::string& tableName);
};

}  // namespace precompiled

}  // namespace dev