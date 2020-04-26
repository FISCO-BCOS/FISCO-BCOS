/*
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
 */

/**
 * @brief: unit test for libconsensus/pbft/PBFTReqCache.h
 * @file: PBFTReqCache.h
 * @author: yujiechen
 * @date: 2018-10-12
 */
#pragma once
#include "Common.h"
#include <libconsensus/pbft/PBFTReqCache.h>
#include <boost/test/unit_test.hpp>
using namespace dev::consensus;
using namespace dev::eth;

namespace dev
{
namespace test
{
static PrepareReq FakePrepareReq(KeyPair& key_pair)
{
    key_pair = KeyPair::create();
    h256 block_hash = crypto::Hash("key_pair");
    PrepareReq req(key_pair, 1000, 1, 134, block_hash);
    return req;
}
/// fake the Sign/Commit request
template <typename T, typename S>
void FakeInvalidReq(PrepareReq::Ptr prepare_req, PBFTReqCache& reqCache, S& cache,
    BlockHeader& highest, h256 const& invalid_hash, size_t invalidHeightNum, size_t invalidHash,
    size_t validNum, bool should_fake = true)
{
    if (should_fake)
    {
        KeyPair key_pair = KeyPair::create();
        *prepare_req = FakePrepareReq(key_pair);
        prepare_req->block_hash = highest.hash();
        prepare_req->height = highest.number();
    }
    /// fake invalid block height
    for (size_t i = 0; i < invalidHeightNum; i++)
    {
        std::shared_ptr<T> req =
            std::make_shared<T>(*prepare_req, KeyPair::create(), prepare_req->idx);
        /// update height of req
        req->height -= (i + 1);
        reqCache.addReq(req, cache);
        BOOST_CHECK(reqCache.getSizeFromCache(prepare_req->block_hash, cache) == i + 1);
    }
    /// fake invalid hash
    for (size_t i = 0; i < invalidHash; i++)
    {
        std::shared_ptr<T> req =
            std::make_shared<T>(*prepare_req, KeyPair::create(), prepare_req->idx);
        req->block_hash = invalid_hash;
        reqCache.addReq(req, cache);
    }
    for (size_t i = 0; i < validNum; i++)
    {
        std::shared_ptr<T> req =
            std::make_shared<T>(*prepare_req, KeyPair::create(), prepare_req->idx);
        req->height += 1;
        reqCache.addReq(req, cache);
        BOOST_CHECK(
            reqCache.getSizeFromCache(prepare_req->block_hash, cache) == invalidHeightNum + i + 1);
    }
}

}  // namespace test
}  // namespace dev
