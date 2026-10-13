/*
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
 * @file EthLaneGenesisFixture.h
 * @brief Shared config.genesis snippet for tests whose fixture sets executor.version >= 2.
 */

#pragma once

#include <string>

namespace bcos::test
{
// NodeConfig::validateL2Invariants binds the Ethereum lane (executor.version >= 2) to a
// non-empty [alloc.*] section and a full [eth_genesis_header] section, both ways. Any test
// fixture that sets executor.version=2/3 must carry these sections or the lane binding
// throws before the guard under test. The header values are the fixture from
// test_NodeConfigEthGenesisHeader.cpp (the hash matches the 21 fields, though only
// Ledger::buildGenesisBlock checks that).
inline std::string ethLaneGenesisSections()
{
    return "[alloc.0]\naddress=0x43000000000000000000000000000000000000C0\n"
           "balance=0\nnonce=0\ncode=0x6080604052\n"
           "[eth_genesis_header]\n"
           "parent_hash=0x0000000000000000000000000000000000000000000000000000000000000000\n"
           "sha3_uncles=0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347\n"
           "miner=0x4200000000000000000000000000000000000011\n"
           "state_root=0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421\n"
           "transactions_root=0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421\n"
           "receipts_root=0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421\n"
           "logs_bloom=0x" +
           std::string(512, '0') +
           "\n"
           "difficulty=0x0\nnumber=0x0\ngas_limit=0x1c9c380\ngas_used=0x0\n"
           "timestamp=0x689d5c00\n"
           "extra_data=0x01000000fa000000060000000000000000\n"
           "mix_hash=0x0000000000000000000000000000000000000000000000000000000000000000\n"
           "nonce=0x0000000000000000\nbase_fee_per_gas=0x3b9aca00\n"
           "withdrawals_root=0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421\n"
           "blob_gas_used=0x0\nexcess_blob_gas=0x0\n"
           "parent_beacon_block_root="
           "0x0000000000000000000000000000000000000000000000000000000000000000\n"
           "requests_hash=0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\n"
           "hash=0x8634eabcf9e6df6b91b63cecab2d7af50a0a4fb8e0cc0aaca07cd8d0da32c069\n";
}

// The [ethGenesisHeader] block generateGenesisData emits for the sections above (u256 /
// integral fields print decimal; hashes print 0x-prefixed hex). Used by the byte-exact
// genesis-pin test.
inline std::string ethLaneGenesisPinSection()
{
    return "[ethGenesisHeader]\n"
           "parent_hash:0x0000000000000000000000000000000000000000000000000000000000000000\n"
           "sha3_uncles:0x1dcc4de8dec75d7aab85b567b6ccd41ad312451b948a7413f0a142fd40d49347\n"
           "miner:0x4200000000000000000000000000000000000011\n"
           "state_root:0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421\n"
           "transactions_root:0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421\n"
           "receipts_root:0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421\n"
           "logs_bloom:0x" +
           std::string(512, '0') +
           "\n"
           "difficulty:0\nnumber:0\ngas_limit:30000000\ngas_used:0\n"
           "timestamp:1755143168\n"
           "extra_data:0x01000000fa000000060000000000000000\n"
           "mix_hash:0x0000000000000000000000000000000000000000000000000000000000000000\n"
           "nonce:0x0000000000000000\n"
           "base_fee_per_gas:1000000000\n"
           "withdrawals_root:0x56e81f171bcc55a6ff8345e692c0f86e5b48e01b996cadc001622fb5e363b421\n"
           "blob_gas_used:0\nexcess_blob_gas:0\n"
           "parent_beacon_block_root:"
           "0x0000000000000000000000000000000000000000000000000000000000000000\n"
           "requests_hash:0xe3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\n"
           "hash:0x8634eabcf9e6df6b91b63cecab2d7af50a0a4fb8e0cc0aaca07cd8d0da32c069\n";
}
}  // namespace bcos::test
