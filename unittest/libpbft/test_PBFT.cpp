#include <boost/test/unit_test.hpp>
#include <json/json.h>

#include <libpbftseal/PBFT.h>
#include <unittest/Common.h>

using namespace dev;
using namespace dev::eth;

namespace test_PBFT {

struct PBFTFixture {
	PBFTFixture() {
	}

	~PBFTFixture() {
		//什么也不做
	}

};

BOOST_FIXTURE_TEST_SUITE(PBFT, PBFTFixture)

BOOST_AUTO_TEST_CASE(asyncSendMessage) {
	//channelSession->asyncSendMessage(request, callback, timeout)
}

BOOST_AUTO_TEST_SUITE_END()

}
