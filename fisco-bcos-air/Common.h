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
#include "bcos-utilities/Common.h"
#include <ctime>
#include <iostream>


namespace bcos::node
{
class ExitHandler
{
public:
    static void exit() { exitHandler(0); }
    static void exitHandler(int signal)
    {
        std::cout << "[" << bcos::getCurrentDateTime() << "] "
                  << "exit because receive signal " << signal << std::endl;
        ExitHandler::c_shouldExit.store(true);
        ExitHandler::c_shouldExit.notify_all();
    }
    static bool shouldExit() { return ExitHandler::c_shouldExit.load(); }

    static boost::atomic_bool c_shouldExit;
};
boost::atomic_bool ExitHandler::c_shouldExit = {false};

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
