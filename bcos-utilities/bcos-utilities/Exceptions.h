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
 * @brief define basic Exceptions
 * @file Exceptions.h
 */

#pragma once
#include "Common.h"
#include <boost/exception/diagnostic_information.hpp>
#include <boost/exception/errinfo_api_function.hpp>
#include <boost/exception/exception.hpp>
#include <boost/exception/info.hpp>
#include <boost/exception/info_tuple.hpp>
#include <boost/stacktrace.hpp>
#include <boost/stacktrace/stacktrace_fwd.hpp>
#include <boost/throw_exception.hpp>
#include <boost/tuple/tuple.hpp>
#include <exception>
#include <string>

namespace bcos
{
using errinfo_comment = boost::error_info<struct tag_comment, std::string>;
using errinfo_stacktrace = boost::error_info<struct tag_stack_trace, boost::stacktrace::stacktrace>;
struct Exception : virtual std::exception, virtual boost::exception
{
    const char* what() const noexcept override;
};

// NOLINTBEGIN(cppcoreguidelines-macro-usage)
/// construct a new exception class overriding Exception
#define DERIVE_BCOS_EXCEPTION(X)         \
    struct X : virtual ::bcos::Exception \
    {                                    \
    }
// NOLINTEND(cppcoreguidelines-macro-usage)

DERIVE_BCOS_EXCEPTION(InvalidParameter);

template <class Exception>
void throwTrace(const Exception& error)
{
    throw boost::enable_error_info(error) << errinfo_stacktrace{boost::stacktrace::stacktrace()};
}
}  // namespace bcos
