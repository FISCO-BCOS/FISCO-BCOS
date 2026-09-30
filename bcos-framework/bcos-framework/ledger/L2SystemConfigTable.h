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
 * @file L2SystemConfigTable.h
 * @brief The state table name L2ConfigLoaderImpl reads the SystemConfig predeploy from.
 *
 * Kept apart from L2ConfigLoader.h on purpose: this header needs EVMAccount.h, and
 * L2ConfigLoader.h is compiled into the bcos-framework unity TU (via the placeholder
 * L2ConfigLoader.cpp), where MSVC 14.51 rejects EVMAccount.h's constexpr system-address
 * constant (C7595/C2131 at EVMAccount.h:71/89/101). Nothing in BCOS_FRAMEWORK_SOURCES may
 * include this file; its callers are libinitializer (OpSystemConfigLoader.h) and the executor
 * unit tests, neither of which the Windows job builds (-DFULLNODE=OFF -DTESTS=OFF).
 */
#pragma once
#include <bcos-framework/ledger/EVMAccount.h>
#include <bcos-framework/ledger/L2ConfigLoader.h>
#include <bcos-utilities/FixedBytes.h>
#include <string>

namespace bcos::ledger
{
/// The state table the SystemConfig predeploy's slots live in, in THIS node's physical
/// layout. Genesis imports the alloc through account::ethLaneAccountTableName
/// (importGenesisAccount in bcos-ledger/Ledger.cpp) and the OP executor reads/writes the
/// account through the same rule, so the loader must derive its key the same way:
/// "/apps/<hex>" on a Hex-layout node, "/s/<20 raw bytes>" on a Binary-layout one. Building
/// "/apps/" + hex by hand reads an empty table on Binary nodes and the loader reports every
/// key as missing.
inline std::string l2SystemConfigTableName()
{
    return account::ethLaneAccountTableName(bcos::Address{
        L2_SYSTEM_CONFIG_ADDRESS_HEX, bcos::Address::FromHex, bcos::Address::AlignRight});
}
}  // namespace bcos::ledger
