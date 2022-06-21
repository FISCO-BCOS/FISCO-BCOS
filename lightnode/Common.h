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
 * @file Common.h
 * @author: yujiechen
 * @date 2021-06-11
 */

#pragma once
#include <atomic>
#include <chrono>
#include <ctime>
#include <iostream>
#include <memory>

namespace bcos
{
namespace node
{
class ExitHandler
{
public:
    void exit() { exitHandler(0); }
    static void exitHandler(int) { ExitHandler::c_shouldExit.store(true); }
    bool shouldExit() const { return ExitHandler::c_shouldExit.load(); }

    static std::atomic_bool c_shouldExit;
};
std::atomic_bool ExitHandler::c_shouldExit = {false};

void setDefaultOrCLocale()
{
#if __unix__
    if (!std::setlocale(LC_ALL, ""))
    {
        setenv("LC_ALL", "C", 1);
    }
#endif
}
}  // namespace node
}  // namespace bcos