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

/// Thrown when an unexpected BCOS-specific field is encountered in an L2 data structure (spec
/// §5.2). Forward-declared here so M3 PRs (Builder classify path) can throw without re-touching
/// this header. If M3 ships without using it, this declaration must be removed in that PR rather
/// than left as dead code.
DERIVE_BCOS_EXCEPTION(UnexpectedBCOSFieldInL2);

/// Thrown when an account row carries a field name the builder has never been taught to classify
/// (spec §5.2). Distinct from UnexpectedBCOSFieldInL2: that one means a KNOWN BCOS field reached
/// an Ethereum-compatible chain, this one means nobody has yet judged whether the field belongs
/// in the state commitment at all. Adding a new account row is what triggers it; the fix is one
/// line in KNOWN_BCOS_EXTENSION_FIELDS once that judgement is made.
DERIVE_BCOS_EXCEPTION(UnknownAccountRowField);

/// Thrown when a read asks for a state root the path-addressed node store cannot serve.
///
/// Node rows are keyed by POSITION, and a position holds exactly ONE version — the current one.
/// So a root the caller supplies is only readable while it IS the store's current root: the walk
/// reads the trie root at position "" and compares keccak(bytes) against the requested root, and
/// a disagreement means the caller asked about an older version whose bytes that position no
/// longer holds. Restoring those reads needs the trie-node history index (spec appendix B), which
/// this PR does not build — hence a loud, specific failure rather than silently answering with
/// today's state, which would be a wrong answer indistinguishable from a right one.
///
/// Distinct from MPTInvariantViolation on purpose: a mismatch at a CHILD position (a hash the
/// parent just told us to expect) is corruption, while a mismatch at the ROOT is the ordinary
/// "that version is gone" outcome of keeping one version per position.
DERIVE_BCOS_EXCEPTION(MPTHistoryUnavailable);

}  // namespace bcos::ledger::mpt
