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
#include "FakeModule.h"
#include "WebsocketServer.h"
#include <libdevcrypto/Common.h>
#include <libethcore/CommonJS.h>
#include <librpc/Rpc.h>
#include <librpc/SafeHttpServer.h>

INITIALIZE_EASYLOGGINGPP

using namespace dev;
using namespace dev::rpc;
using namespace dev::ledger;
using namespace dev::demo;


using tcp = boost::asio::ip::tcp;
namespace websocket = boost::beast::websocket;

int main(int argc, const char* argv[])
{
#if 1

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

#if 0

    auto m_service = std::make_shared<MockService>();
    std::string configurationPath = "";
    KeyPair m_keyPair = KeyPair::create();
    auto m_ledgerManager = std::make_shared<LedgerManager>(m_service, m_keyPair);
    m_ledgerManager->initSingleLedger<FakeLedger>(1, "", configurationPath);

    auto rpc = new Rpc(m_ledgerManager, m_service);

    using FullServer = ModularServer<rpc::Rpc>;

    ModularServer<>* jsonrpcHttpServer = NULL;
    jsonrpcHttpServer = new FullServer(rpc);
    std::string listenIP = "127.0.0.1";
    int listenPort = 30301;
    int httpListenPort = 30302;
    std::shared_ptr<dev::SafeHttpServer> _safeHttpServer;
    _safeHttpServer.reset(
        new SafeHttpServer(listenIP, httpListenPort), [](SafeHttpServer* p) { (void)p; });
    jsonrpcHttpServer->addConnector(_safeHttpServer.get());
    jsonrpcHttpServer->StartListening();
    LOG(INFO) << "JsonrpcHttpServer started.";
    sleep(10000);

#endif
}
