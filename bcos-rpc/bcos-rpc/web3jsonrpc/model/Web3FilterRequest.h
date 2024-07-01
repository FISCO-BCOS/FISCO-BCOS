/**
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file FilterRequest.h
 * @author: kyonGuo
 * @date 2024/4/11
 */

#pragma once
#include <bcos-rpc/filter/FilterRequest.h>
#include <bcos-rpc/web3jsonrpc/utils/Common.h>

namespace bcos::rpc
{

class Web3FilterRequest : public FilterRequest
{
public:
    Web3FilterRequest() = default;
    ~Web3FilterRequest() = default;
    Web3FilterRequest(const FilterRequest& oth) : FilterRequest(oth) {}
    virtual int32_t InvalidParamsCode() override { return Web3JsonRpcError::Web3DefautError; }
};

class Web3FilterRequestFactory : public FilterRequestFactory
{
public:
    Web3FilterRequestFactory() = default;
    ~Web3FilterRequestFactory() = default;
    FilterRequest::Ptr create() override { return std::make_shared<Web3FilterRequest>(); }
    FilterRequest::Ptr create(const FilterRequest& req) override
    {
        return std::make_shared<Web3FilterRequest>(req);
    }
};

}  // namespace bcos::rpc