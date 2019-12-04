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
#include <libprecompiled/Common.h>

#include <libdevcore/Common.h>
#include <libethcore/Common.h>

namespace dev
{
namespace storage
{
class Table;
}

namespace precompiled
{
class DagTransferPrecompiled : public dev::blockverifier::Precompiled
{
public:
    typedef std::shared_ptr<DagTransferPrecompiled> Ptr;
    DagTransferPrecompiled();
    virtual ~DagTransferPrecompiled(){};

    std::string toString() override;

    bytes call(dev::blockverifier::ExecutiveContext::Ptr context, bytesConstRef param,
        Address const& origin = Address()) override;

public:
    // is this precompiled need parallel processing, default false.
    virtual bool isParallelPrecompiled() override { return true; }
    virtual std::vector<std::string> getParallelTag(bytesConstRef param) override;

protected:
    std::shared_ptr<storage::Table> openTable(
        dev::blockverifier::ExecutiveContext::Ptr context, Address const& origin);

public:
    bool invalidUserName(const std::string& user);
    void userAddCall(dev::blockverifier::ExecutiveContext::Ptr context, bytesConstRef data,
        Address const& origin, bytes& out);
    void userSaveCall(dev::blockverifier::ExecutiveContext::Ptr context, bytesConstRef data,
        Address const& origin, bytes& out);
    void userDrawCall(dev::blockverifier::ExecutiveContext::Ptr context, bytesConstRef data,
        Address const& origin, bytes& out);
    void userBalanceCall(dev::blockverifier::ExecutiveContext::Ptr context, bytesConstRef data,
        Address const& origin, bytes& out);
    void userTransferCall(dev::blockverifier::ExecutiveContext::Ptr context, bytesConstRef data,
        Address const& origin, bytes& out);
};

}  // namespace precompiled

}  // namespace dev
