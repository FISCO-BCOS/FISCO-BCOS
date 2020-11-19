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
/** @file Initializer.h
 *  @author chaychen
 *  @modify first draft
 *  @date 20181022
 */

#pragma once

#include "BoostLogInitializer.h"
#include "InitializerInterface.h"
#include "LedgerInitializer.h"
#include "P2PInitializer.h"
#include "RPCInitializer.h"
#include "SecureInitializer.h"
#include <memory>

namespace bcos
{
namespace initializer
{
class SecureInitializer;
class P2PInitializer;
class LedgerInitializer;
class RPCInitializer;
class LogInitializer;
class Initializer : public InitializerInterface, public std::enable_shared_from_this<Initializer>
{
public:
    typedef std::shared_ptr<Initializer> Ptr;

    virtual ~Initializer();
    void init(std::string const& _path);

    std::shared_ptr<SecureInitializer> secureInitializer() { return m_secureInitializer; }
    std::shared_ptr<P2PInitializer> p2pInitializer() { return m_p2pInitializer; }
    std::shared_ptr<LedgerInitializer> ledgerInitializer() { return m_ledgerInitializer; }
    std::shared_ptr<RPCInitializer> rpcInitializer() { return m_rpcInitializer; }
    std::shared_ptr<LogInitializer> logInitializer() { return m_logInitializer; }

private:
    std::shared_ptr<LogInitializer> m_logInitializer;
    std::shared_ptr<RPCInitializer> m_rpcInitializer;
    std::shared_ptr<LedgerInitializer> m_ledgerInitializer;
    std::shared_ptr<P2PInitializer> m_p2pInitializer;

    std::shared_ptr<SecureInitializer> m_secureInitializer;
};

}  // namespace initializer

}  // namespace bcos
