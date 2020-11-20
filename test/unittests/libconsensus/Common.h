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
 * @brief: unit test for Common.h of libconsensus module
 * @file: Common.cpp
 * @author: yujiechen
 * @date: 2018-10-11
 */
#pragma once
#include <libconfig/GlobalConfigure.h>
#include <libconsensus/Common.h>
#include <libconsensus/pbft/Common.h>
#include <libdevcrypto/Common.h>
#include <libdevcrypto/SM2Signature.h>
#include <boost/test/unit_test.hpp>

namespace bcos
{
namespace test
{
template <typename T>
void checkPBFTMsg(T const& msg, KeyPair const _keyPair = KeyPair::create(),
    int64_t const& _height = -1, bcos::consensus::VIEWTYPE const& _view = bcos::consensus::MAXVIEW,
    bcos::consensus::IDXTYPE const& _idx = bcos::consensus::MAXIDX,
    u256 const& _timestamp = Invalid256, h256 const& _blockHash = h256())
{
    BOOST_CHECK(msg.height == _height);
    BOOST_CHECK(msg.view == _view);
    BOOST_CHECK(msg.idx == _idx);
    BOOST_CHECK(msg.timestamp == _timestamp);
    BOOST_CHECK(msg.block_hash == _blockHash);
    if (!msg.sig.empty())
    {
        if (g_BCOSConfig.SMCrypto())
        {
            bool result =
                crypto::Verify(_keyPair.pub(), sm2SignatureFromBytes(msg.sig), msg.block_hash);
            BOOST_CHECK_EQUAL(result, true);
        }
        else
        {
            BOOST_CHECK_EQUAL(toHex(msg.sig), toHex(msg.signHash(msg.block_hash, _keyPair)));
        }
    }
    if (!msg.sig2.empty())
    {
        if (g_BCOSConfig.SMCrypto())
        {
            bool result1 = crypto::Verify(
                _keyPair.pub(), sm2SignatureFromBytes(msg.sig2), msg.fieldsWithoutBlock());
            BOOST_CHECK_EQUAL(result1, true);
        }
        else
        {
            BOOST_CHECK_EQUAL(
                toHex(msg.sig2), toHex(msg.signHash(msg.fieldsWithoutBlock(), _keyPair)));
        }
    }
}

}  // namespace test
}  // namespace bcos
