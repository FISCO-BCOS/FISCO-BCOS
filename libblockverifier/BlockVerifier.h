/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file BlockVerifier.h
 *  @author mingzhenliu
 *  @date 20180921
 */
#pragma once

#include "ExecutiveContextFactory.h"
#include <ExecutiveContext.h>
#include <Precompiled.h>
#include <libdevcore/FixedHash.h>
#include <libdevcore/easylog.h>
#include <libdevcrypto/Common.h>
#include <libethcore/Block.h>
#include <libethcore/Transaction.h>
#include <memory>
namespace dev
{
namespace blockverifier
{
class BlockVerifier : public std::enable_shared_from_this<BlockVerifier>
{
public:
    typedef std::shared_ptr<BlockVerifier> Ptr;

    BlockVerifier(){};

    virtual ~BlockVerifier(){};

    ExecutiveContext::Ptr executeBlock(const Block& block, int stateType,
        std::unordered_map<Address, PrecompiledContract> const& precompiledContract);

    TransactionReceipt executeTransation(const Transaction& transaction);

    void setExecutiveContextFactory(ExecutiveContextFactory::Ptr executiveContextFactory)
    {
        m_executiveContextFactory = executiveContextFactory;
    }

    ExecutiveContextFactory::Ptr getExecutiveContextFactory() { return m_executiveContextFactory; }

private:
    // bool exeTransation(const Transaction& transaction, ExecutiveContext::Ptr context);

    ExecutiveContextFactory::Ptr m_executiveContextFactory;
};

}  // namespace blockverifier

}  // namespace dev
