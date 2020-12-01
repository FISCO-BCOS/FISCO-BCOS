/**
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
 *
 * @brief Unit tests for the Common
 * @file Common.cpp
 * @author: chaychen
 * @date 2018
 */

#include "libdevcrypto/CryptoInterface.h"
#include <libdevcrypto/Common.h>
#include <libethcore/BlockHeader.h>
#include <libethcore/Common.h>
#include <libethcore/Exceptions.h>
#include <libutilities/JsonDataConvertUtility.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <string>

using namespace bcos;
using namespace bcos::eth;

namespace bcos
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(Common, TestOutputHelperFixture)

BOOST_AUTO_TEST_CASE(testBadBlock)
{
    BlockHeader blockHeader;
    blockHeader.setParentHash(crypto::Hash("parent"));
    blockHeader.setRoots(
        crypto::Hash("transactionRoot"), crypto::Hash("receiptRoot"), crypto::Hash("stateRoot"));
    blockHeader.setLogBloom(LogBloom(0));
    blockHeader.setNumber(0);
    blockHeader.setGasLimit(u256(3000000));
    blockHeader.setGasUsed(u256(100000));
    uint64_t current_time = utcTime();
    blockHeader.setTimestamp(current_time);
    blockHeader.appendExtraDataArray(jonStringToBytes("0x1020"));
    blockHeader.setSealer(u256("0x00"));
    h512s sealer_list;
    for (unsigned int i = 0; i < 10; i++)
    {
        sealer_list.push_back(toPublic(Secret::generateRandomFixedBytes()));
    }
    blockHeader.setSealerList(sealer_list);

    RLPStream blockHeaderStream;
    bytes blockHeaderBytes;
    blockHeader.encode(blockHeaderBytes);
    badBlock(blockHeaderBytes, "This blockheader no bad.");
}

BOOST_AUTO_TEST_CASE(testToAddress)
{
    BOOST_CHECK_NO_THROW(bcos::eth::toAddress("0x64fa644d2a694681bd6addd6c5e36cccd8dcdde3"));
    BOOST_CHECK_THROW(bcos::eth::toAddress("0x64fa644d"), InvalidAddress);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace bcos
