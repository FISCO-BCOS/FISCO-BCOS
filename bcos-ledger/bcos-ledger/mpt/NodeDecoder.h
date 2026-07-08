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
 * @file NodeDecoder.h
 * @brief RLP node decoding for MPT nodes — inverse of NodeEncoder (spec §5.5)
 */
#pragma once

#include "TrieNode.h"
#include <bcos-utilities/Common.h>

namespace bcos::ledger::mpt
{

/// Reconstruct a TrieNode from its canonical RLP encoding (inverse of encodeRaw).
///
/// The decoder mirrors the wire format produced by NodeEncoder:
///   {0x80}                                  → EmptyNode
///   2-item list [HP(leaf=1), value]         → LeafNode (keyNibbles have no HP terminator)
///   2-item list [HP(leaf=0), child_ref_raw] → ExtensionNode (child kept RLP-encoded)
///   17-item list [c0..c15, value]           → BranchNode (absent/Hash/Inline children)
///
/// @throws MPTDecodeError on malformed input (truncated header, span past end, wrong item count).
TrieNode decodeNode(bcos::bytesConstRef raw);

}  // namespace bcos::ledger::mpt
