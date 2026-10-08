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
 * @file TestPrinters.h
 * @brief Printers for BOOST.Test failure messages on types with no operator<<.
 */

#pragma once

#include <bcos-utilities/Common.h>

#include <boost/test/unit_test.hpp>

#include <string>

// bcos::u256/s256 are boost::multiprecision::number<...> typedefs: a free
// operator<< in namespace bcos is unreachable by ADL (Boost.Test's printer
// looks in boost::multiprecision). Specialize the boost printer trait instead.
// h256/h160 (FixedBytes<N>) already have operator<< (FixedBytes.h:470) — do NOT
// duplicate them here.
namespace boost::test_tools
{
template <>
struct print_log_value<bcos::u256>
{
    void operator()(std::ostream& out, bcos::u256 const& v) { out << v.str(); }
};

template <>
struct print_log_value<bcos::s256>
{
    void operator()(std::ostream& out, bcos::s256 const& v) { out << v.str(); }
};
}  // namespace boost::test_tools
