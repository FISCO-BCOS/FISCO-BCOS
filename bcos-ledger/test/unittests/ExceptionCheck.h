/**
 *  Copyright (C) 2024 FISCO BCOS.
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
 * @file ExceptionCheck.h
 * @brief BOOST_CHECK_EXCEPTION predicate pinning the errinfo_comment text.
 */
#pragma once
#include <bcos-utilities/Exceptions.h>
#include <string>

namespace bcos::test
{
// BOOST_CHECK_THROW pins only the exception TYPE, so a wrong-field throw (same
// type, different message) still passes. Use with BOOST_CHECK_EXCEPTION to pin
// a distinctive substring of the errinfo_comment as well:
//   BOOST_CHECK_EXCEPTION(stmt, bcos::tool::InvalidConfig,
//       [](auto const& e) { return errinfoContains(e, "address must be exactly 40"); });
template <typename Exception>
inline bool errinfoContains(Exception const& e, std::string const& needle)
{
    auto const* msg = boost::get_error_info<bcos::errinfo_comment>(e);
    return msg != nullptr && msg->find(needle) != std::string::npos;
}
}  // namespace bcos::test
