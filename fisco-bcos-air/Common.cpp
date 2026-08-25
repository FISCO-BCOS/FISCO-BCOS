/**
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
 * @file Common.cpp
 * @author: yujiechen
 * @date 2021-06-11
 */

#include "Common.h"
#include <clocale>
#include <cstdlib>
#include <boost/atomic.hpp>

namespace bcos::node
{
// Static member definitions — defined here rather than in the header to
// uphold the One Definition Rule.
boost::atomic_bool ExitHandler::c_shouldExit = {false};
struct sigaction ExitHandler::s_oldTERM = {};
struct sigaction ExitHandler::s_oldINT = {};
struct sigaction ExitHandler::s_oldABRT = {};
struct sigaction ExitHandler::s_oldSEGV = {};

void setDefaultOrCLocale()
{
#if __unix__
    if (std::setlocale(LC_ALL, "") == nullptr)
    {
        setenv("LC_ALL", "C", 1);
    }
#endif
}
}  // namespace bcos::node
