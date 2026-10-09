/*
 *  Copyright (C) 2021 FISCO BCOS.
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
 * @file Protocol.cppm
 *  Named module interface unit for bcos-protocol (module bcos.protocol).
 *
 *  Dual-track design: the public headers remain the source of truth and are
 *  included textually in the global module fragment below, so every entity an
 *  importer sees is attached to the global module and is ODR-identical to the
 *  same entity seen through a textual #include in another TU. The export
 *  using-declarations only re-export those existing declarations.
 *
 *  GCC note (PR99000/PR114600): GCC does not merge BMI-loaded declarations
 *  with declarations from a textual #include that appears AFTER an import in
 *  the same TU (it errors with "redefinition"). The supported mixing order is:
 *  all textual #includes first, then import declarations. Consuming TUs must
 *  therefore place `import bcos.protocol;` after their #include block.
 */
module;

#include <bcos-protocol/TransactionStatus.h>
#include <bcos-protocol/Common.h>
#include <bcos-protocol/TransactionSubmitResultImpl.h>
#include <bcos-protocol/TransactionSubmitResultFactoryImpl.h>
#include <bcos-protocol/amop/TopicItem.h>

export module bcos.protocol;

// Re-export the public surface of the five bcos-protocol headers. Macros
// (DERIVE_BCOS_EXCEPTION, BCOS_LOG, ...) cannot cross a module boundary; code
// that uses them directly must still #include the corresponding header.
export namespace bcos::protocol
{
// TransactionStatus.h
using bcos::protocol::TransactionStatus;
using bcos::protocol::operator<<;
using bcos::protocol::toString;

// Common.h
using bcos::protocol::PBObjectEncodeException;
using bcos::protocol::PBObjectDecodeException;
using bcos::protocol::decodePBObject;
using bcos::protocol::encodePBObject;

// TransactionSubmitResultImpl.h / TransactionSubmitResultFactoryImpl.h
using bcos::protocol::TransactionSubmitResultFactoryImpl;
using bcos::protocol::TransactionSubmitResultImpl;

// amop/TopicItem.h
using bcos::protocol::operator<;
using bcos::protocol::parseSubTopicsJson;
using bcos::protocol::TopicItem;
using bcos::protocol::TopicItems;
}  // namespace bcos::protocol
