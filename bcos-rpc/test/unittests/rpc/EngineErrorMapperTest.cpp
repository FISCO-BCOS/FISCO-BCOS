/**
 *  Copyright (C) 2026 FISCO BCOS.
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
 * @file EngineErrorMapperTest.cpp
 * @brief Unit tests for the engine-exception -> execution-apis JSON-RPC error-code mapping
 */

#include <bcos-framework/engine/Errors.h>
#include <bcos-rpc/web3jsonrpc/utils/Common.h>
#include <bcos-rpc/web3jsonrpc/utils/EngineErrorMapper.h>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::rpc;

BOOST_AUTO_TEST_SUITE(EngineErrorMapperTest)

BOOST_AUTO_TEST_CASE(mapsEngineExceptionTypesToExecutionApiCodes)
{
    // execution-apis codes (exec-engine.md references execution-apis error table).
    BOOST_CHECK_EQUAL(
        mapEngineErrorCode(bcos::engine::UnsupportedFork{}), EngineError::UnsupportedFork);
    BOOST_CHECK_EQUAL(mapEngineErrorCode(bcos::engine::IncompatiblePayloadVersion{}),
        EngineError::UnsupportedFork);
    BOOST_CHECK_EQUAL(
        mapEngineErrorCode(bcos::engine::UnknownPayload{}), EngineError::UnknownPayload);
    BOOST_CHECK_EQUAL(mapEngineErrorCode(bcos::engine::InvalidForkchoiceState{}),
        EngineError::InvalidForkchoiceState);
    BOOST_CHECK_EQUAL(mapEngineErrorCode(bcos::engine::UnsupportedOpPayloadAttributes{}),
        EngineError::InvalidPayloadAttributes);
    // Everything not enumerated stays the generic internal error.
    BOOST_CHECK_EQUAL(mapEngineErrorCode(bcos::engine::OpExecutionInternalError{}), InternalError);
    BOOST_CHECK_EQUAL(
        mapEngineErrorCode(bcos::engine::UnsupportedEngineApiVersion{}), InternalError);
    BOOST_CHECK_EQUAL(mapEngineErrorCode(bcos::Error{BCOS_ERROR(1, "x")}), InternalError);
}

BOOST_AUTO_TEST_SUITE_END()
