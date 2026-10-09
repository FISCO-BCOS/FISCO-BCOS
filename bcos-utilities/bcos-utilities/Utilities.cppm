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
 * @file Utilities.cppm
 *  Named module interface unit for bcos-utilities (module bcos.utilities).
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
 *  therefore place `import bcos.utilities;` after their #include block.
 *
 *  Macros (BCOS_LOG, LOG_KV, DERIVE_BCOS_EXCEPTION, BCOS_ERROR, ...) cannot
 *  cross a module boundary; code that uses them directly must still #include
 *  the corresponding header. The log macros live in the lightweight
 *  LogStream.h (no boost/log dependency), so this is cheap. Entities with
 *  internal linkage (Invalid256, ZeroAddress) cannot be exported either;
 *  their users likewise keep a textual #include.
 *
 *  Deliberately NOT in the module: Bloom.h (pulls bcos-crypto/bcos-framework
 *  into the GMF closure, breaking the layering), BoostLog.h /
 *  BoostLogInitializer.h / BoostLogCollector.h (every importer deserializes
 *  the whole GMF, so the boost/log closure must stay out — the log facade
 *  lives in the lightweight LogStream.h),
 *  ITTAPI.h (vendor), Log.h (alias of LogStream.h), TestPromptFixture.h
 *  (test-only), and the boost::asio-based headers Timer.h / NewTimer.h /
 *  Worker.h / IOServicePool.h / RateCollector.h — GCC (-fmodules-ts) emits
 *  asio's header-defined thread_local template statics
 *  (keyword_tss_ptr<...>::value_) with strong external linkage in every
 *  importing TU, causing "multiple definition" at link time.
 */
module;

#include <bcos-utilities/RefDataContainer.h>
#include <bcos-utilities/Common.h>
#include <bcos-utilities/ThreeWay4Apple.h>
#include <bcos-utilities/Exceptions.h>
#include <bcos-utilities/Error.h>
#include <bcos-utilities/DataConvertUtility.h>
#include <bcos-utilities/FixedBytes.h>
#include <bcos-utilities/LogStream.h>
#include <bcos-utilities/AnyHolder.h>
#include <bcos-utilities/Base64.h>
#include <bcos-utilities/BucketMap.h>
#include <bcos-utilities/FileUtility.h>
#include <bcos-utilities/GzTools.h>
#include <bcos-utilities/JsonDataConvertUtility.h>
#include <bcos-utilities/Overloaded.h>
#include <bcos-utilities/ZstdCompress.h>

export module bcos.utilities;

// Re-export the public surface of the bcos-utilities headers. The internal-
// linkage constants Invalid256 (Common.h) and ZeroAddress (FixedBytes.h) are
// TU-local and must not appear here.
export namespace bcos
{
// RefDataContainer.h
using ::bcos::RefDataContainer;
using ::bcos::ref;

// Common.h
using ::bcos::bigint;
using ::bcos::byte;
using ::bcos::bytes;
using ::bcos::BytesMap;
using ::bcos::bytesConstPtr;
using ::bcos::bytesConstRef;
using ::bcos::bytesPointer;
using ::bcos::bytesRef;
using ::bcos::calcAvgQPS;
using ::bcos::calcAvgRate;
using ::bcos::errorExit;
using ::bcos::getCurrentDateTime;
using ::bcos::Guard;
using ::bcos::hex2u;
using ::bcos::HexMap;
using ::bcos::isalNumStr;
using ::bcos::isHexStrWithPrefix;
using ::bcos::isNumStr;
using ::bcos::Mutex;
using ::bcos::NullBytes;
using ::bcos::pthread_getThreadName;
using ::bcos::pthread_getThreadNameRef;
using ::bcos::pthread_setThreadName;
using ::bcos::ReadGuard;
using ::bcos::RecursiveGuard;
using ::bcos::RecursiveMutex;
using ::bcos::s160;
using ::bcos::s256;
using ::bcos::s512;
using ::bcos::s2u;
using ::bcos::SharedMutex;
using ::bcos::string32;
using ::bcos::toMillisecond;
using ::bcos::u160;
using ::bcos::u256;
using ::bcos::u512;
using ::bcos::u2s;
using ::bcos::UniqueGuard;
using ::bcos::UpgradableGuard;
using ::bcos::UpgradeGuard;
using ::bcos::utcSteadyTime;
using ::bcos::utcSteadyTimeUs;
using ::bcos::utcTime;
using ::bcos::utcTimeUs;
using ::bcos::WriteGuard;

// Exceptions.h
using ::bcos::errinfo_comment;
using ::bcos::errinfo_stacktrace;
using ::bcos::Exception;
using ::bcos::InvalidParameter;
using ::bcos::throwTrace;

// Overloaded.h
using ::bcos::overloaded;

// Error.h
using ::bcos::Error;

// DataConvertUtility.h
using ::bcos::asBytes;
using ::bcos::asString;
using ::bcos::fromBigEndian;
using ::bcos::fromBigQuantity;
using ::bcos::fromHex;
using ::bcos::fromHexWithPrefix;
using ::bcos::fromQuantity;
using ::bcos::isHexString;
using ::bcos::isHexStringV2;
using ::bcos::operator+;
using ::bcos::operator+=;
using ::bcos::safeCastToU256;
using ::bcos::safeFromBigQuantity;
using ::bcos::safeFromHex;
using ::bcos::safeFromHexWithPrefix;
using ::bcos::safeFromQuantity;
using ::bcos::toBigEndian;
using ::bcos::toCompactBigEndian;
using ::bcos::toCompactBigEndianString;
using ::bcos::toHex;
using ::bcos::toHexStringWithPrefix;
using ::bcos::toPaddingHexStringWithPrefix;
using ::bcos::toQuantity;

// FixedBytes.h
using ::bcos::Address;
using ::bcos::AddressHash;
using ::bcos::Addresses;
using ::bcos::asAddress;
using ::bcos::FixedBytes;
using ::bcos::fromAddress;
using ::bcos::h1024;
using ::bcos::h128;
using ::bcos::h160;
using ::bcos::h160Hash;
using ::bcos::h160s;
using ::bcos::h160Set;
using ::bcos::h2048;
using ::bcos::h256;
using ::bcos::h256Hash;
using ::bcos::h256s;
using ::bcos::h256Set;
using ::bcos::h512;
using ::bcos::h512s;
using ::bcos::h520;
using ::bcos::h64;
using ::bcos::left160;
using ::bcos::operator<<;
using ::bcos::operator>>;
using ::bcos::right160;
using ::bcos::toAddress;
using ::bcos::toString;

// LogStream.h (macros BCOS_LOG/LOG_KV/... are not exportable; they expand to
// bcos::LogStream and no longer need any boost/log header)
using ::bcos::c_fileLogLevel;
using ::bcos::LogLevel;
using ::bcos::LogStream;
using ::bcos::operator<=>;
using ::bcos::setFileLogLevel;

// AnyHolder.h
using ::bcos::AnyHolder;
using ::bcos::InPlace;

// Base64.h
using ::bcos::base64Decode;
using ::bcos::base64DecodeBytes;
using ::bcos::base64Encode;

// BucketMap.h
using ::bcos::Bucket;
using ::bcos::BucketMap;
using ::bcos::BucketSet;
using ::bcos::EmptyType;
using ::bcos::RapidHasher;
using ::bcos::StringHash;

// FileUtility.h
using ::bcos::readContents;
using ::bcos::readContentsToString;

// GzTools.h
using ::bcos::createGzFile;

// JsonDataConvertUtility.h
using ::bcos::jonStringToBytes;
using ::bcos::jonStringToFixedBytes;
using ::bcos::jonStringToU256;
using ::bcos::jsonStringToAddress;
using ::bcos::jsonStringToInt;
using ::bcos::toJonString;

// ZstdCompress.h
using ::bcos::ZstdCompress;
}  // namespace bcos

// Common.h adds an operator<< overload set for bcos::bytes/bytesConstRef in
// namespace std; ordinary name lookup cannot find non-exported GMF
// declarations, so re-export the overload set explicitly.
export using std::operator<<;
