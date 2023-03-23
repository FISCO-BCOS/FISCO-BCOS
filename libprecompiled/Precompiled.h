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
/** @file Precompiled.h
 *  @author mingzhenliu
 *  @date 20180921
 */
#pragma once

#include "Common.h"
#include "PrecompiledResult.h"
#include <libdevcore/Address.h>
#include <libstorage/Table.h>
#include <map>
#include <memory>

namespace dev
{
namespace storage
{
class Table;
}  // namespace storage
namespace blockverifier
{
class ExecutiveContext;
}
namespace precompiled
{
class Precompiled : public std::enable_shared_from_this<Precompiled>
{
public:
    typedef std::shared_ptr<Precompiled> Ptr;

    virtual ~Precompiled() noexcept = default;

    virtual std::string toString() { return ""; }
    virtual PrecompiledExecResult::Ptr call(
        std::shared_ptr<dev::blockverifier::ExecutiveContext> _context, bytesConstRef _param,
        Address const& _origin = Address(), Address const& _sender = Address()) = 0;

    // is this precompiled need parallel processing, default false.
    virtual bool isParallelPrecompiled() { return false; }
    virtual std::vector<std::string> getParallelTag(bytesConstRef /*param*/)
    {
        return std::vector<std::string>();
    }
    virtual uint32_t getFuncSelector(std::string const& _functionName);

    void setPrecompiledExecResultFactory(
        PrecompiledExecResultFactory::Ptr _precompiledExecResultFactory)
    {
        m_precompiledExecResultFactory = _precompiledExecResultFactory;
    }

    PrecompiledExecResultFactory::Ptr precompiledExecResultFactory()
    {
        return m_precompiledExecResultFactory;
    }

protected:
    std::map<std::string, uint32_t> name2Selector;

protected:
    uint64_t getEntriesCapacity(std::shared_ptr<dev::storage::Entries const> _entries) const;

    std::shared_ptr<dev::storage::Table> createTable(
        std::shared_ptr<dev::blockverifier::ExecutiveContext> _context,
        const std::string& _tableName, const std::string& _keyField, const std::string& _valueField,
        Address const& origin = Address());
    bool checkAuthority(std::shared_ptr<dev::blockverifier::ExecutiveContext> context,
        Address const& _origin, Address const& _contract);

    PrecompiledExecResultFactory::Ptr m_precompiledExecResultFactory;
};
// for UT
void clearName2SelectCache();

}  // namespace precompiled
}  // namespace dev
