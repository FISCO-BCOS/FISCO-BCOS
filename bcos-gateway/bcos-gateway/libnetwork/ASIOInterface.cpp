/**
 * @brief: inteface for boost::asio(for unittest)
 *
 * @file AsioInterface.cpp
 * @author: bxq2011hust
 * @date 2019-07-244
 */
#include <bcos-gateway/libnetwork/ASIOInterface.h>

namespace ba = boost::asio;
namespace bi = ba::ip;
using namespace bcos;
using namespace bcos::gateway;
using namespace std;

void ASIOInterface::asyncResolveConnect(std::shared_ptr<SocketFace> socket, Handler_Type handler)
{
    auto protocol = socket->nodeIPEndpoint().isIPv6() ? bi::tcp::tcp::v6() : bi::tcp::tcp::v4();
    m_resolver->async_resolve(protocol, socket->nodeIPEndpoint().address(),
        to_string(socket->nodeIPEndpoint().port()),
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
