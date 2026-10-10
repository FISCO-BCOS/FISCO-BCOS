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
 * @file BlockImportSummary.h
 * @brief Outcome of one offline block-import run; standalone so that callers
 *        naming the return type (e.g. AirNodeInitializer.h) do not have to
 *        include the whole importer.
 * @date 2026/10/9
 */
#pragma once

#include <bcos-utilities/FixedBytes.h>
#include <cstddef>
#include <cstdint>

namespace bcos::initializer
{

/// Outcome of one offline import run. `skipped` counts blocks that failed decoding,
/// verification, or commit — each was logged at WARNING and the import continued
/// (hive blocks/*.rlp files are independent inputs; a bad one must not abort the run).
struct BlockImportSummary
{
    size_t imported = 0;
    size_t skipped = 0;
    int64_t headNumber = -1;  // ledger head after the run (0 = still at genesis)
    bcos::h256 headHash;      // hash of the head header (genesis anchor on a fresh ledger)
};

}  // namespace bcos::initializer
