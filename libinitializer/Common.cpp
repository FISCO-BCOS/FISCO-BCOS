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
* @brief Common for libinitializer
* @file Common.cpp
* @author: jimmyshi
* @date 2022-01-13
*/

#include "Common.h"
#include <include/BuildInfo.h>

using namespace std;

namespace bcos
{
namespace initializer
{

void printVersion()
{
    std::cout << "FISCO BCOS Version : " << FISCO_BCOS_PROJECT_VERSION << std::endl;
    std::cout << "Build Time         : " << FISCO_BCOS_BUILD_TIME << std::endl;
    std::cout << "Build Type         : " << FISCO_BCOS_BUILD_PLATFORM << "/"
              << FISCO_BCOS_BUILD_TYPE << std::endl;
    std::cout << "Git Branch         : " << FISCO_BCOS_BUILD_BRANCH << std::endl;
    std::cout << "Git Commit         : " << FISCO_BCOS_COMMIT_HASH << std::endl;
}
}
}