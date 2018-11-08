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
/** @file Initializer.h
 *  @author chaychen
 *  @modify first draft
 *  @date 20181022
 */

#pragma once

#include "Common.h"
#include "CommonInitializer.h"
#include "InitializerInterface.h"
#include "LedgerInitiailizer.h"
#include "P2PInitializer.h"
#include "RPCInitiailizer.h"
#include "SecureInitializer.h"

namespace dev
{
namespace initializer
{
class Initializer : public InitializerInterface, public std::enable_shared_from_this<Initializer>
{
public:
    typedef std::shared_ptr<Initializer> Ptr;

    void init(std::string const& _path);

    CommonInitializer::Ptr commonInitializer() { return m_commonInitializer; }
    SecureInitializer::Ptr secureInitiailizer() { return m_secureInitiailizer; }
    P2PInitializer::Ptr p2pInitializer() { return m_p2pInitializer; }
    LedgerInitiailizer::Ptr ledgerInitiailizer() { return m_ledgerInitiailizer; }
    RPCInitiailizer::Ptr rpcInitiailizer() { return m_rpcInitiailizer; }

private:
    CommonInitializer::Ptr m_commonInitializer;
    LedgerInitiailizer::Ptr m_ledgerInitiailizer;
    P2PInitializer::Ptr m_p2pInitializer;
    RPCInitiailizer::Ptr m_rpcInitiailizer;
    SecureInitializer::Ptr m_secureInitiailizer;
};

}  // namespace initializer

}  // namespace dev
