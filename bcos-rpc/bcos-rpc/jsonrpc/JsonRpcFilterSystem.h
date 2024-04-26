#pragma once

#include <bcos-rpc/filter/FilterRequest.h>
#include <bcos-rpc/jsonrpc/Common.h>

namespace bcos
{
namespace rpc
{

class JsonRpcFilterRequest : public FilterRequest
{
public:
    JsonRpcFilterRequest() = default;
    ~JsonRpcFilterRequest() = default;
    JsonRpcFilterRequest(const FilterRequest& oth) : FilterRequest(oth) {}
    virtual int32_t InvalidParamsCode() override { return JsonRpcError::InvalidParams; }
};

class JsonRpcFilterRequestFactory : public FilterRequestFactory
{
public:
    JsonRpcFilterRequestFactory() = default;
    ~JsonRpcFilterRequestFactory() = default;
    FilterRequest::Ptr create() override { return std::make_shared<JsonRpcFilterRequest>(); }
    FilterRequest::Ptr create(const FilterRequest& req) override
    {
        return std::make_shared<JsonRpcFilterRequest>(req);
    }
};

// class JsonRpcFilterSystem;

}  // namespace rpc
}  // namespace bcos