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
 * @file Errors.h
 * @brief Exception types for the MPT module (spec §5.5, §5.2)
 */
#pragma once

#include <bcos-utilities/Exceptions.h>

namespace bcos::ledger::mpt
{

/// Thrown when decoding of HexPrefix or RLP-encoded trie data fails
DERIVE_BCOS_EXCEPTION(MPTDecodeError);

/// Thrown when an internal MPT invariant is violated (e.g. odd nibble count passed to
/// nibblesToBytes)
DERIVE_BCOS_EXCEPTION(MPTInvariantViolation);

/// Thrown when an unexpected BCOS-specific field is encountered in an L2 data structure (spec §5.2)
DERIVE_BCOS_EXCEPTION(UnexpectedBCOSFieldInL2);

}  // namespace bcos::ledger::mpt
