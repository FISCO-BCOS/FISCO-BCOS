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
#include <boost/exception/diagnostic_information.hpp>
#include <boost/exception/exception.hpp>
#include <boost/throw_exception.hpp>
#include <exception>
#include <memory>
#include <string>

#define BCOS_ERROR(errorCode, errorMessage) \
    ::bcos::Error::buildError(BOOST_CURRENT_FUNCTION, __FILE__, __LINE__, errorCode, errorMessage)
#define BCOS_ERROR_WITH_PREV(errorCode, errorMessage, prev) \
    ::bcos::Error::buildError(                              \
        BOOST_CURRENT_FUNCTION, __FILE__, __LINE__, errorCode, errorMessage, prev)

#define BCOS_ERROR_PTR(code, message) std::make_shared<Error>(BCOS_ERROR(code, message))
#define BCOS_ERROR_WITH_PREV_PTR(code, message, prev) \
    std::make_shared<Error>(BCOS_ERROR_WITH_PREV(code, message, prev))

#define BCOS_ERROR_UNIQUE_PTR(code, message) std::make_unique<Error>(BCOS_ERROR(code, message))
#define BCOS_ERROR_WITH_PREV_UNIQUE_PTR(code, message, prev) \
    std::make_unique<Error>(BCOS_ERROR_WITH_PREV(code, message, prev))

namespace bcos
{
class Error : public bcos::Exception
{
public:
    using Ptr = std::shared_ptr<Error>;
    using ConstPtr = std::shared_ptr<const Error>;

    using UniquePtr = std::unique_ptr<Error>;
    using UniqueConstPtr = std::unique_ptr<const Error>;

    using PrevStdError = boost::error_info<struct PrevErrorTag, std::string>;

    static Error buildError(char const* func, char const* file, int line, int32_t errorCode,
        const std::string& errorMessage)
    {
        Error error(errorCode, errorMessage);
        error << boost::throw_function(func);
        error << boost::throw_file(file);
        error << boost::throw_line(line);

        return error;
    }

    static Error buildError(char const* func, char const* file, int line, int32_t errorCode,
        const std::string& errorMessage, const Error& prev)
    {
        auto error = buildError(func, file, line, errorCode, errorMessage);
        error << PrevStdError(boost::diagnostic_information(prev));
        return error;
    }

    static Error buildError(char const* func, char const* file, int line, int32_t errorCode,
        const std::string& errorMessage, const std::exception& prev)
    {
        auto error = buildError(func, file, line, errorCode, errorMessage);
        error << PrevStdError(boost::diagnostic_information(prev));
        return error;
    }

    Error() = default;
    Error(int64_t _errorCode, std::string _errorMessage)
      : bcos::Exception(_errorMessage),
        m_errorCode(_errorCode),
        m_errorMessage(std::move(_errorMessage))
    {}

    virtual ~Error() {}

    virtual int64_t errorCode() const { return m_errorCode; }
    virtual std::string const& errorMessage() const { return m_errorMessage; }

    virtual void setErrorCode(int64_t _errorCode) { m_errorCode = _errorCode; }
    virtual void setErrorMessage(std::string const& _errorMessage)
    {
        m_errorMessage = _errorMessage;
    }

private:
    int64_t m_errorCode = 0;
    std::string m_errorMessage;
};
}  // namespace bcos