#include <boost/test/unit_test.hpp>
#include <json/json.h>

#include <libchannelserver/ChannelSession.h>
#include <unittest/Common.h>

using namespace dev;
using namespace dev::channel;

namespace test_ChannelSession {

struct ChannelSessionFixture {
	ChannelSessionFixture() {
		channelSession = std::make_shared<dev::channel::ChannelSession>();
	}

	~ChannelSessionFixture() {
		
	}

	dev::channel::ChannelSession::Ptr channelSession;
};

BOOST_FIXTURE_TEST_SUITE(ChannelSession, ChannelSessionFixture)

BOOST_AUTO_TEST_CASE(asyncSendMessage) {
	//channelSession->asyncSendMessage(request, callback, timeout)
}

BOOST_AUTO_TEST_SUITE_END()

}
