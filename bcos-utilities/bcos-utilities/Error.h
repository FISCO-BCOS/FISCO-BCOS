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
 * @file Error.h
 * @author: yujiechen
 * @date: 2021-04-07
 */
#pragma once
#include "Common.h"
#include "Exceptions.h"
#include <boost/assert/source_location.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/exception/exception.hpp>
#include <boost/throw_exception.hpp>
#include <exception>
#include <memory>
#include <string>

#define BCOS_ERROR(errorCode, errorMessage) \
    ::bcos::Error::buildError(BOOST_CURRENT_LOCATION.to_string(), errorCode, errorMessage)
#define BCOS_ERROR_WITH_PREV(errorCode, errorMessage, prev) \
    ::bcos::Error::buildError(BOOST_CURRENT_LOCATION.to_string(), errorCode, errorMessage, prev)

#define BCOS_ERROR_PTR(code, message) std::make_shared<bcos::Error>(BCOS_ERROR(code, message))
#define BCOS_ERROR_WITH_PREV_PTR(code, message, prev) \
    std::make_shared<bcos::Error>(BCOS_ERROR_WITH_PREV(code, message, prev))

#define BCOS_ERROR_UNIQUE_PTR(code, message) \
    std::make_unique<bcos::Error>(BCOS_ERROR(code, message))
#define BCOS_ERROR_WITH_PREV_UNIQUE_PTR(code, message, prev) \
    std::make_unique<bcos::Error>(BCOS_ERROR_WITH_PREV(code, message, prev))

namespace bcos
{

class Error : public bcos::Exception
{
public:
    using Ptr = std::shared_ptr<Error>;
    using ConstPtr = std::shared_ptr<const Error>;

    using UniquePtr = std::unique_ptr<Error>;
    using UniqueConstPtr = std::unique_ptr<const Error>;

    // using PrevStdError = boost::error_info<struct PrevErrorTag, std::string>;
    using Context = boost::error_info<struct ErrorContext, std::string>;
    using ErrorCode = boost::error_info<struct ErrorCodeTag, int64_t>;
    using ErrorMessage = boost::error_info<struct ErrorMessageTag, std::string>;
    using PrevError = boost::error_info<struct ErrorTag, Error>;
    using STDError = boost::error_info<struct STDErrorTag, std::exception_ptr>;
    using STDErrorMessage = boost::error_info<struct STDErrorTag, std::string>;

    static Error buildError(
        std::string context, int32_t errorCode, std::string errorMessage, Error prev)
    {
        auto error = buildError(std::move(context), errorCode, std::move(errorMessage));
        error << PrevError(std::move(prev));
        return error;
    }

    static Error buildError(std::string context, int32_t errorCode, std::string errorMessage,
        const std::exception& prev)
    {
        auto error = buildError(std::move(context), errorCode, std::move(errorMessage));
        error << STDErrorMessage(prev.what());
        error << STDError(std::make_exception_ptr(prev));
        return error;
    }

    static Error buildError(std::string context, int32_t errorCode, std::string errorMessage)
    {
        Error error;
        error << Context(std::move(context));
        error << ErrorCode(errorCode);
        error << ErrorMessage(std::move(errorMessage));
        return error;
    }

    virtual int64_t errorCode() const
    {
        const auto* ptr = boost::get_error_info<ErrorCode>(*this);
        if (ptr != nullptr)
        {
            return *ptr;
        }
        return 0;
    }
    virtual std::string errorMessage() const
    {
        const auto* ptr = boost::get_error_info<ErrorMessage>(*this);
        if (ptr != nullptr)
        {
            return *ptr;
        }
        return {};
    }

    virtual void setErrorCode(int64_t errorCode) { *this << ErrorCode(errorCode); }
    virtual void setErrorMessage(std::string errorMessage)
    {
        *this << ErrorMessage(std::move(errorMessage));
    }

    virtual std::string toString() const
    {
        std::stringstream ss;
        ss << "code:" << errorCode() << ", msg:" << errorMessage();
        return ss.str();
    }

    Error() = default;

private:
};
}  // namespace bcos