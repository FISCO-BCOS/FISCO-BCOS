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
 * @file HalfCommitChecker.h
 * @brief Half-commit (orphan block) detection over a node state database (spec §5.12)
 */
#pragma once

#include "bcos-framework/protocol/BlockFactory.h"
#include "bcos-framework/protocol/ProtocolTypeDef.h"
#include <rocksdb/db.h>
#include <iosfwd>
#include <string>
#include <vector>

namespace bcos::ledger::halfcommit
{

// A block whose tx-index rows exist above current_number: the crash happened
// after the block data was stored but before the state commit advanced
// current_number.
struct OrphanBlock
{
    protocol::BlockNumber blockNumber{};
    // Decoded from the SYS_NUMBER_2_TXS row (a BlockFactory-encoded block
    // carrying transaction metadata only; see Ledger::asyncPrewriteBlock).
    std::vector<crypto::HashType> txHashes;
};

struct ScanReport
{
    protocol::BlockNumber currentNumber{-1};
    std::vector<OrphanBlock> orphans;  // ascending blockNumber
    bool dbCorrupted{false};
    std::string corruptionReason;
};

// Read-only scan shared by node startup (PR-29) and the offline CLI:
// 1. read SYS_CURRENT_STATE.current_number = M
// 2. collect rows of SYS_NUMBER_2_TXS with blockNumber > M (orphans)
// 3. cross-check against SYS_BLOCK_NUMBER_2_NONCES — the two index tables are
//    written together (Ledger::asyncPrewriteBlock), so the block-number sets
//    above M must be identical; a mismatch is reported as dbCorrupted (caller
//    should abort and involve ops, never self-heal)
//
// Never modifies the database. Values are read as ledger wrote them; a
// database with storage_security (disk encryption) enabled is not supported.
ScanReport scan(::rocksdb::DB& db, protocol::BlockFactory& blockFactory);

// Human-readable diagnostic, one line per orphan block.
std::string formatReport(ScanReport const& report);

// One "<blockNumber> <0xTxHash>" per line; format is grep/awk-friendly and
// consumed by ops runbooks — keep it stable.
void dumpOrphans(ScanReport const& report, std::ostream& out);

}  // namespace bcos::ledger::halfcommit
