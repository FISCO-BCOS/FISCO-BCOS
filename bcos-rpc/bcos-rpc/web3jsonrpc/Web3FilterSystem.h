#pragma once

#include <bcos-rpc/filter/FilterSystem.h>
#include <bcos-rpc/web3jsonrpc/model/Web3FilterRequest.h>
#include <bcos-rpc/web3jsonrpc/utils/Common.h>

namespace bcos
{
namespace rpc
{

class Web3FilterSystem : public FilterSystem
{
public:
    Web3FilterSystem(GroupManager::Ptr groupManager, const std::string& groupId, int filterTimeout,
        int maxBlockProcessPerReq)
      : FilterSystem(groupManager, groupId, std::make_shared<Web3FilterRequestFactory>(),
            filterTimeout, maxBlockProcessPerReq)
    {}

    virtual int32_t InvalidParamsCode() override { return Web3JsonRpcError::Web3DefautError; }
};

}  // namespace rpc
}  // namespace bcos