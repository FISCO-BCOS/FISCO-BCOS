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
/** @file SecureInitiailizer.h
 *  @author chaychen
 *  @modify first draft
 *  @date 20181022
 */

#pragma once

#include "Common.h"
#include <libp2p/CertificateServer.h>
#include <boost/asio.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>

namespace bas = boost::asio::ssl;
namespace dev
{
namespace initializer
{
class SecureInitiailizer : public std::enable_shared_from_this<SecureInitiailizer>
{
public:
    typedef std::shared_ptr<SecureInitiailizer> Ptr;

    void initConfig(boost::property_tree::ptree const& _pt);

    std::shared_ptr<bas::context> SSLContext() { return m_SSLContext; }

private:
    std::shared_ptr<bas::context> m_SSLContext;
};

}  // namespace initializer

}  // namespace dev