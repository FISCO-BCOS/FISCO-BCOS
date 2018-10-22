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
 * @brief: empty framework for main of lab-bcos
 *
 * @file: p2p_main.cpp
 * @author: yujiechen
 * @date 2018-09-10
 */

#include <fisco-bcos/ParamParse.h>
#include <json_spirit/JsonSpiritHeaders.h>
#include <libdevcore/easylog.h>
#include <libp2p/CertificateServer.h>
#include <libp2p/Host.h>
#include <libp2p/Network.h>
#include <libp2p/P2pFactory.h>
#include <libp2p/Service.h>
#include <boost/program_options.hpp>
#include <memory>

using namespace dev;
using namespace dev::p2p;
namespace js = json_spirit;

class P2PMessageFactory: public MessageFactory {
public:
	virtual ~P2PMessageFactory() {}
	virtual Message::Ptr buildMessage() override {
		return std::make_shared<Message>();
	}
};

static h512 node1 = h512(
    "7dcce48da1c464c7025614a54a4e26df7d6f92cd4d315601e057c1659796736c5c8730e380fcbe637191cc2aebf474"
    "6846c0db2604adebf9c70c7f418d4d5a61");
static h512 node2 = h512(
    "46787132f4d6285bfe108427658baf2b48de169bdb745e01610efd7930043dcc414dc6f6ddc3da6fc491cc1c15f46e"
    "621ea7304a9b5f0b3fb85ba20a6b1c0fc1");
static h512 node3 = h512(
    "78c4bfd1de7a732d94a5bdf90bcf8340d3ed73dade707d95d2caf70be5b4de6c21784d21749b1455a8ef4e9e9ca8b5"
    "031ca2c17680b122f649245b3958ff6913");
static h512 node4 = h512(
    "f6f4931f56b9963851f43bb857ed5a6170ec1a4208ddcf1a1f2bb66f6d7e7a5c4749a89b5277d6265b1c12fdbc8929"
    "0bed7cccf905eef359989275319b331753");
static h512 node5 = h512(
    "4525966a74f9b2ba564fcceee50bf1c4d2faa35a307983df026a18b3486da6ad5bf4b9a56a8d7e35484af2cafb6224"
    "059c4ee332c9ec02bd304c0292c2f9efb8");
static h512 node6 = h512(
    "658616ebd2477bad1bebc517c97b605a947532550f895c8161d7f48f341ef6af63bf814a93f7182fd2f57e275d80be"
    "5b46ba664a36bc232c354ac3f2ea82e3bf");
static h512 node7 = h512(
    "113ea98e53722a4b411e0450e71cb018720355b110d45dbf441ff3a89b36f009c4ae4b66ab243fc0660444c0ce71fa"
    "3bb1e8af9b0a70cfcada5c36f884bebf65");

static void InitNetwork(Params& m_params)
{
    std::shared_ptr<AsioInterface> m_asioInterface = std::make_shared<AsioInterface>();
    std::shared_ptr<NetworkConfig> m_netConfig = m_params.creatNetworkConfig();
    /// create m_socketFactory
    std::shared_ptr<SocketFactory> m_socketFactory = std::make_shared<SocketFactory>();
    /// create m_sessionFactory
    std::shared_ptr<SessionFactory> m_sessionFactory = std::make_shared<SessionFactory>();
    std::shared_ptr<Host> m_host =
        std::make_shared<Host>(m_params.clientVersion(), CertificateServer::GetInstance().keypair(),
            *m_netConfig.get(), m_asioInterface, m_socketFactory, m_sessionFactory);
    m_host->setStaticNodes(m_params.staticNodes());
    m_host->setMessageFactory(std::make_shared<P2PMessageFactory>());
    /// set the period for nodes to update topics
    uint32_t counter = 0;
    uint32_t interval = 0;
    if (node1 == m_host->id())
    {
        interval = 6;
    }
    else if (node2 == m_host->id())
    {
        interval = 15;
    }
    else if (node3 == m_host->id())
    {
        interval = 10;
    }
    else if (node4 == m_host->id())
    {
        interval = 1;
    }
    auto p2pMsgHandler = std::make_shared<P2PMsgHandler>();
    auto service = std::make_shared<Service>(m_host, p2pMsgHandler);
    /// begin working
    m_host->start();
    while (true)
    {
        /// update my topics periodically
        counter++;
        if (counter % interval == 0)
        {
            std::shared_ptr<std::vector<std::string>> topics = m_host->topics();
            std::string topic = "Topic" + to_string(counter);
            topics->push_back(topic);
            LOG(INFO) << "Add topic periodically, now Topics[" << topics->size() - 1
                      << "]:" << topic;
            m_host->setTopics(topics);
        }
        this_thread::sleep_for(chrono::milliseconds(1000));
    }
}

int main(int argc, const char* argv[])
{
    /// init params
    Params m_params = initCommandLine(argc, argv);
    /// init p2p
    InitNetwork(m_params);
}
