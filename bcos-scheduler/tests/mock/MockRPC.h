#pragma once

#include "../../bcos-scheduler/Common.h"
#include <bcos-framework/interfaces/rpc/RPCInterface.h>
#include <boost/test/unit_test.hpp>
#include <boost/thread/latch.hpp>

namespace bcos::test
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

class MockRPC : public bcos::rpc::RPCInterface
{
public:
    void start() override {}
    void stop() override {}

    void asyncNotifyBlockNumber(std::string const& _groupID, std::string const& _nodeName,
        bcos::protocol::BlockNumber _blockNumber,
        std::function<void(Error::Ptr)> _callback) override
    {}

    void asyncNotifyGroupInfo(
        bcos::group::GroupInfo::Ptr _groupInfo, std::function<void(Error::Ptr&&)>) override
    {}

    boost::latch* latch = nullptr;
};

#pragma GCC diagnostic pop

}  // namespace bcos::test