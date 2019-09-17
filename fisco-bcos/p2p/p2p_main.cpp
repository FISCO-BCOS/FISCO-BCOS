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
 * @brief: empty framework for main of FISCO-BCOS
 *
 * @file: p2p_main.cpp
 * @author: yujiechen
 * @date 2018-09-10
 */


#include <libdevcore/TopicInfo.h>
#include <libinitializer/Initializer.h>
#include <libinitializer/P2PInitializer.h>
#include <stdlib.h>


using namespace std;
using namespace dev;
using namespace dev::initializer;

int main()
{
    auto initialize = std::make_shared<Initializer>();
    initialize->init("./config.ini");

    auto p2pInitializer = initialize->p2pInitializer();
    auto p2pService = p2pInitializer->p2pService();
    /// p2pService->setMessageFactory(std::make_shared<P2PMessageFactory>());

    uint32_t counter = 0;
    while (true)
    {
        std::shared_ptr<std::set<std::string>> p_topics = std::shared_ptr<std::set<std::string>>();
        std::string topic = "Topic" + to_string(counter++);
        P2PMSG_LOG(TRACE) << "Add topic periodically, now Topics[" << p_topics->size() - 1
                          << "]:" << topic;
        p_topics->insert(topic);
        p2pService->setTopics(p_topics);
        LogInitializer::logRotateByTime();
        this_thread::sleep_for(chrono::milliseconds((rand() % 50) * 100));
    }

    return 0;
}
