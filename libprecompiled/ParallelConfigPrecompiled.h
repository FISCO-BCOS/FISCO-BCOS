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
/** @file ParallelConfigPrecompiled.h
 *  @author jimmyshi
 *  @date 20190315
 */
#pragma once
#include "Common.h"
#include <libblockverifier/ExecutiveContext.h>
#include <libethcore/ABI.h>

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
struct ParallelConfig
{
    typedef std::shared_ptr<ParallelConfig> Ptr;
    std::string functionName;
    u256 criticalSize;
};

const std::string PARA_CONFIG_TABLE_PREFIX = "_contract_parafunc_";
const std::string PARA_CONFIG_TABLE_PREFIX_SHORT = "cp_";

class ParallelConfigPrecompiled : public dev::precompiled::Precompiled
{
public:
    typedef std::shared_ptr<ParallelConfigPrecompiled> Ptr;
    ParallelConfigPrecompiled();
    virtual ~ParallelConfigPrecompiled(){};

    std::string toString() override;

    PrecompiledExecResult::Ptr call(std::shared_ptr<dev::blockverifier::ExecutiveContext> context,
        bytesConstRef param, Address const& origin = Address(),
        Address const& _sender = Address()) override;

    dev::storage::Table::Ptr openTable(dev::blockverifier::ExecutiveContext::Ptr context,
        Address const& contractAddress, Address const& origin, bool needCreate = true);

private:
    void registerParallelFunction(dev::blockverifier::ExecutiveContext::Ptr context,
        bytesConstRef data, Address const& origin, bytes& out);
    void unregisterParallelFunction(dev::blockverifier::ExecutiveContext::Ptr context,
        bytesConstRef data, Address const& origin, bytes& out);

public:
    /// get paralllel config, return nullptr if not found
    ParallelConfig::Ptr getParallelConfig(dev::blockverifier::ExecutiveContext::Ptr context,
        Address const& contractAddress, uint32_t selector, Address const& origin);
};

}  // namespace precompiled

}  // namespace dev