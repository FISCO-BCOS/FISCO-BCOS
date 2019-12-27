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
 * @file rpc_main.cpp
 * @author: caryliao
 * @date 2018-11-2
 */
#include "WebsocketServer.h"
#include <libdevcrypto/Common.h>
#include <libethcore/CommonJS.h>
#include <librpc/Rpc.h>
#include <librpc/SafeHttpServer.h>


using namespace dev;
using namespace dev::rpc;
using namespace dev::ledger;
using namespace dev::initializer;
using namespace rpcdemo;


using tcp = boost::asio::ip::tcp;
namespace websocket = boost::beast::websocket;

int main()
{
#if 0
	/// websocket demo
	auto const address = boost::asio::ip::make_address("127.0.0.1");
    auto const port = static_cast<unsigned short>(std::atoi("30302"));
    auto const threads = 1;
    // The io_context is required for all I/O
    boost::asio::io_context ioc{threads};

    // Create and launch a listening port
    std::make_shared<listener>(ioc, tcp::endpoint{address, port})->run();

    std::vector<std::thread> v;
    v.reserve(threads - 1);
    for (auto i = threads - 1; i > 0; --i)
        v.emplace_back([&ioc] { ioc.run(); });
    ioc.run();

#endif


    /// http demo
    auto initialize = std::make_shared<Initializer>();
    initialize->init("./config.ini");

    auto secureInitializer = initialize->secureInitializer();
    // KeyPair key_pair = secureInitializer->keyPair();
    auto ledgerInitializer = initialize->ledgerInitializer();

    auto p2pInitializer = initialize->p2pInitializer();
    auto p2pService = p2pInitializer->p2pService();

    auto rpc = new Rpc(ledgerInitializer, p2pService);

    ModularServer<>* jsonrpcHttpServer = new ModularServer<rpc::Rpc>(rpc);
    std::string listenIP = "127.0.0.1";
    int httpListenPort = 30302;
    jsonrpcHttpServer->addConnector(new SafeHttpServer(listenIP, httpListenPort));
    jsonrpcHttpServer->StartListening();
    LOG(INFO) << "JsonrpcHttpServer started.";
    sleep(10000);
    delete jsonrpcHttpServer;
}
