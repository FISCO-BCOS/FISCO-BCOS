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
 * @file EthGenesisHeaderTest.cpp
 * @brief Unit tests for toEthBlockHeaderData (genesis artifact -> EthBlockHeaderData)
 * @date 2026/9/14
 */

#include "bcos-rlp-protocol/EthGenesisHeader.h"
#include <boost/test/unit_test.hpp>
#include <algorithm>

using namespace bcos;
using namespace bcos::protocol;

namespace bcos::test
{
BOOST_AUTO_TEST_SUITE(EthGenesisHeaderMappingTest)

template <size_t N>
static FixedBytes<N> filled(uint8_t value)
{
    FixedBytes<N> out;
    std::fill(out.begin(), out.end(), value);
    return out;
}

// The mapping copies every one of the 21 header fields (15 core + 6 fork-gated),
// including the fork-gated optionals when the genesis header carries them.
BOOST_AUTO_TEST_CASE(allFieldsCopied)
{
    ledger::EthGenesisHeader genesis;
    genesis.m_parentHash = filled<32>(0x01);
    genesis.m_sha3Uncles = filled<32>(0x02);
    genesis.m_miner = filled<20>(0x03);
    genesis.m_stateRoot = filled<32>(0x04);
    genesis.m_transactionsRoot = filled<32>(0x05);
    genesis.m_receiptsRoot = filled<32>(0x06);
    genesis.m_logsBloom.assign(256, static_cast<char>(0x07));
    genesis.m_difficulty = 8;
    genesis.m_number = 0;  // forced by NodeConfig's [eth_genesis_header] parser
    genesis.m_gasLimit = 30000000;
    genesis.m_gasUsed = 21000;
    genesis.m_timestamp = 1700000000;
    genesis.m_extraData = {0x09, 0x0a};
    genesis.m_mixHash = filled<32>(0x0b);
    genesis.m_nonce = filled<8>(0x0c);
    genesis.m_baseFeePerGas = 1000000000;
    genesis.m_withdrawalsRoot = filled<32>(0x0e);
    genesis.m_blobGasUsed = 1;
    genesis.m_excessBlobGas = 2;
    genesis.m_parentBeaconBlockRoot = filled<32>(0x11);
    genesis.m_requestsHash = filled<32>(0x12);

    auto h = toEthBlockHeaderData(genesis);
    BOOST_CHECK(h.parentInfo.blockHash == genesis.m_parentHash);
    BOOST_CHECK(h.uncleHash == genesis.m_sha3Uncles);
    BOOST_CHECK(h.coinbase == genesis.m_miner);
    BOOST_CHECK(h.stateRoot == genesis.m_stateRoot);
    BOOST_CHECK(h.txsRoot == genesis.m_transactionsRoot);
    BOOST_CHECK(h.receiptsRoot == genesis.m_receiptsRoot);
    BOOST_CHECK(std::equal(
        h.logsBloom.begin(), h.logsBloom.end(), genesis.m_logsBloom.begin()));
    BOOST_CHECK(h.difficulty == genesis.m_difficulty);
    BOOST_CHECK(h.gasLimit == genesis.m_gasLimit);
    BOOST_CHECK(h.gasUsed == genesis.m_gasUsed);
    BOOST_CHECK(h.number == genesis.m_number);
    BOOST_CHECK(h.timestamp == genesis.m_timestamp);
    BOOST_CHECK(h.extraData == genesis.m_extraData);
    BOOST_CHECK(h.prevRandao == genesis.m_mixHash);
    BOOST_CHECK(h.nonce == genesis.m_nonce);
    BOOST_CHECK(h.baseFee == genesis.m_baseFeePerGas);
    BOOST_CHECK(h.withdrawalsHash == genesis.m_withdrawalsRoot);
    BOOST_CHECK(h.blobGasUsed == genesis.m_blobGasUsed);
    BOOST_CHECK(h.excessBlobGas == genesis.m_excessBlobGas);
    BOOST_CHECK(h.parentBeaconRoot == genesis.m_parentBeaconBlockRoot);
    BOOST_CHECK(h.requestsHash == genesis.m_requestsHash);
}

// A genesis header without the fork-gated keys (pre-Cancun chain): the optionals
// stay nullopt so the RLP re-encoding is byte-exact.
BOOST_AUTO_TEST_CASE(forkGatedFieldsStayAbsent)
{
    ledger::EthGenesisHeader genesis;
    genesis.m_logsBloom.assign(256, static_cast<char>(0x00));
    auto h = toEthBlockHeaderData(genesis);
    BOOST_CHECK(!h.baseFee.has_value());
    BOOST_CHECK(!h.withdrawalsHash.has_value());
    BOOST_CHECK(!h.blobGasUsed.has_value());
    BOOST_CHECK(!h.excessBlobGas.has_value());
    BOOST_CHECK(!h.parentBeaconRoot.has_value());
    BOOST_CHECK(!h.requestsHash.has_value());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
