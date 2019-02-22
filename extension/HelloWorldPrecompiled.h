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
#include <libblockverifier/ExecutiveContext.h>
#include <libdevcore/Common.h>
#include <libethcore/Common.h>
#include <libprecompiled/Common.h>

namespace dev
{
namespace storage
{
class Table;
}

namespace precompiled
{
class HelloWorldPrecompiled : public dev::blockverifier::Precompiled
{
public:
    typedef std::shared_ptr<HelloWorldPrecompiled> Ptr;
    HelloWorldPrecompiled();
    virtual ~HelloWorldPrecompiled(){};

    std::string toString(dev::blockverifier::ExecutiveContext::Ptr) override;

    bytes call(dev::blockverifier::ExecutiveContext::Ptr context, bytesConstRef param,
        Address const& origin = Address()) override;

protected:
    std::shared_ptr<storage::Table> openTable(
        dev::blockverifier::ExecutiveContext::Ptr context, Address const& origin = Address());

private:
    // call HelloWorld get interface
    void get(dev::blockverifier::ExecutiveContext::Ptr context, bytesConstRef data,
        Address const& origin, bytes& out);

    // call HelloWorld set interface
    void set(dev::blockverifier::ExecutiveContext::Ptr context, bytesConstRef data,
        Address const& origin, bytes& out);
};

}  // namespace precompiled

}  // namespace dev