/**
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
 *
 * @brief: inteface for boost::asio(for unittest)
 *
 * @file AsioInterface.cpp
 * @author: bxq2011hust
 * @date 2019-07-244
 */
#include "ASIOInterface.h"

namespace ba = boost::asio;
namespace bi = ba::ip;
using namespace dev::network;

void ASIOInterface::asyncResolveConnect(std::shared_ptr<SocketFace> socket, Handler_Type handler)
{
    m_resolver->async_resolve(socket->nodeIPEndpoint(),
        [=](const boost::system::error_code& ec, bi::tcp::resolver::results_type results) {
            if (!ec)
            {
                // results is a iterator, but only use first endpoint.
                socket->ref().async_connect(results->endpoint(), handler);
                ASIO_LOG(INFO) << LOG_DESC("asyncResolveConnect")
                               << LOG_KV("endpoint", results->endpoint());
            }
            else
            {
                ASIO_LOG(WARNING) << LOG_DESC("asyncResolve failed")
                                  << LOG_KV("host", socket->nodeIPEndpoint().address())
                                  << LOG_KV("port", socket->nodeIPEndpoint().port());
            }
        });
}
