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
 * @brief: unit test for Session
 *
 * @file Session.cpp
 * @author: caryliao
 * @date 2018-10-22
 */

#include <libp2p/P2PSession.h>

#include <libnetwork/Host.h>
#include <libnetwork/Session.h>
#include <libp2p/Service.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>

using namespace dev;
using namespace dev::network;
using namespace dev::p2p;

namespace dev
{
namespace test
{
class MockSession : public dev::network::Session
{
public:
    virtual ~MockSession() {}

    virtual void start() override {}

    virtual void disconnect(dev::network::DisconnectReason) override {}
};

class MockService : public p2p::Service
{
public:
    virtual ~MockService() {}

    virtual bool actived() override { return m_actived; }

    bool m_actived = false;
};

class P2PSessionFixure
{
public:
    P2PSessionFixure()
    {
        mockSession = std::make_shared<MockSession>();
        mockService = std::make_shared<MockService>();
    }

    P2PSession::Ptr newSession()
    {
        auto p2pSession = std::make_shared<P2PSession>();
        p2pSession->setSession(mockSession);
        p2pSession->setService(mockService);

        return p2pSession;
    }

    std::shared_ptr<MockSession> mockSession;
    std::shared_ptr<MockService> mockService;
};

BOOST_FIXTURE_TEST_SUITE(SessionTest, P2PSessionFixure)

BOOST_AUTO_TEST_CASE(start)
{
    auto session = newSession();
    BOOST_CHECK(session->actived() == false);

    session->start();
    BOOST_CHECK(session->actived() == true);
}

BOOST_AUTO_TEST_CASE(stop)
{
    auto session = newSession();
    session->start();
    BOOST_CHECK(session->actived() == true);

    session->stop(UserReason);
    BOOST_CHECK(session->actived() == false);
}

BOOST_AUTO_TEST_CASE(heartBeat)
{
    auto session = newSession();
    session->start();
    BOOST_CHECK(session->actived() == true);

    session->heartBeat();
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
