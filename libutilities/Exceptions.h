/*
 *  Copyright (C) 2020 FISCO BCOS.
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
 * @brief define basic Exceptions
 * @file Exceptions.h
 */

#pragma once
#include "Common.h"
#include "FixedHash.h"
#include <boost/exception/diagnostic_information.hpp>
#include <boost/exception/errinfo_api_function.hpp>
#include <boost/exception/exception.hpp>
#include <boost/exception/info.hpp>
#include <boost/exception/info_tuple.hpp>
#include <boost/throw_exception.hpp>
#include <boost/tuple/tuple.hpp>
#include <exception>
#include <string>

namespace bcos
{
/**
 * @brief : Base class for all exceptions
 */
struct Exception : virtual std::exception, virtual boost::exception
{
    Exception(std::string _message = std::string()) : m_message(std::move(_message)) {}
    const char* what() const noexcept override
    {
        return m_message.empty() ? std::exception::what() : m_message.c_str();
    }

private:
    std::string m_message;
};

/// construct a new exception class overidding Exception
#define DERIVE_BCOS_EXCEPTION(X) \
    struct X : virtual Exception \
    {                            \
    }
// TODO: remove this
/// Base class for all RLP exceptions.
struct RLPException : virtual Exception
{
};

/// construct a new exception class overriding RLPException
#define DERIVE_BCOS_EXCEPTION_RLP(X) \
    struct X : virtual RLPException  \
    {                                \
    }
DERIVE_BCOS_EXCEPTION_RLP(BadCast);
DERIVE_BCOS_EXCEPTION_RLP(BadRLP);
DERIVE_BCOS_EXCEPTION_RLP(OversizeRLP);
DERIVE_BCOS_EXCEPTION_RLP(UndersizeRLP);

DERIVE_BCOS_EXCEPTION(BadHexCharacter);
DERIVE_BCOS_EXCEPTION(RootNotFound);
DERIVE_BCOS_EXCEPTION(BadRoot);
DERIVE_BCOS_EXCEPTION(MissingField);

DERIVE_BCOS_EXCEPTION(NoNetworking);
DERIVE_BCOS_EXCEPTION(FileError);
DERIVE_BCOS_EXCEPTION(Overflow);
DERIVE_BCOS_EXCEPTION(FailedInvariant);

DERIVE_BCOS_EXCEPTION(InterfaceNotSupported);
DERIVE_BCOS_EXCEPTION(InitLedgerConfigFailed);

DERIVE_BCOS_EXCEPTION(StorageError);
DERIVE_BCOS_EXCEPTION(OpenDBFailed);
DERIVE_BCOS_EXCEPTION(DBNotOpened);


DERIVE_BCOS_EXCEPTION(DatabaseError);
DERIVE_BCOS_EXCEPTION(DatabaseNeedRetry);

DERIVE_BCOS_EXCEPTION(WriteDBFailed);
DERIVE_BCOS_EXCEPTION(DecryptFailed);
DERIVE_BCOS_EXCEPTION(EncryptFailed);
DERIVE_BCOS_EXCEPTION(InvalidDiskEncryptionSetting);
DERIVE_BCOS_EXCEPTION(EncryptedDB);

DERIVE_BCOS_EXCEPTION(UnsupportedFeature);

DERIVE_BCOS_EXCEPTION(ForbidNegativeValue);
DERIVE_BCOS_EXCEPTION(InvalidConfiguration);
DERIVE_BCOS_EXCEPTION(InvalidPort);
DERIVE_BCOS_EXCEPTION(InvalidSupportedVersion);

// TODO: simplify
using errinfo_invalidSymbol = boost::error_info<struct tag_invalidSymbol, char>;
using errinfo_wrongAddress = boost::error_info<struct tag_address, std::string>;
using errinfo_comment = boost::error_info<struct tag_comment, std::string>;
using errinfo_required = boost::error_info<struct tag_required, bigint>;
using errinfo_got = boost::error_info<struct tag_got, bigint>;
using errinfo_min = boost::error_info<struct tag_min, bigint>;
using errinfo_max = boost::error_info<struct tag_max, bigint>;
using RequirementError = boost::tuple<errinfo_required, errinfo_got>;
using RequirementErrorComment = boost::tuple<errinfo_required, errinfo_got, errinfo_comment>;
using errinfo_hash256 = boost::error_info<struct tag_hash, h256>;
using errinfo_required_h256 = boost::error_info<struct tag_required_h256, h256>;
using errinfo_got_h256 = boost::error_info<struct tag_get_h256, h256>;
using Hash256RequirementError = boost::tuple<errinfo_required_h256, errinfo_got_h256>;
using errinfo_extraData = boost::error_info<struct tag_extraData, bytes>;
using errinfo_externalFunction = boost::errinfo_api_function;
using errinfo_interface = boost::error_info<struct tag_interface, std::string>;
using errinfo_path = boost::error_info<struct tag_path, std::string>;
}  // namespace bcos
