#include "bcos-cpp-sdk/tarsRPC/RPCClient.h"
#include <boost/test/unit_test.hpp>

struct TarsClientTestFixture
{
};

BOOST_FIXTURE_TEST_SUITE(TarsClientTest, TarsClientTestFixture)

BOOST_AUTO_TEST_CASE(ConnectionString)
{
    std::vector<std::string> hostAndPorts = {"127.0.0.1:1000", "127.0.0.2:2000"};
    std::string connectionString = bcos::sdk::RPCClient::toConnectionString(hostAndPorts);
    BOOST_CHECK_EQUAL(connectionString,
        "fiscobcos.rpc.RPCObj@tcp -h 127.0.0.1 -p 1000:tcp -h 127.0.0.2 -p 2000");
}

BOOST_AUTO_TEST_SUITE_END()