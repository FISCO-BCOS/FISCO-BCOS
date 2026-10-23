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
 * @brief: logging channel names. The logging pipeline no longer goes through
 *  boost::log core: producers hand fully formatted lines to whole-line sinks
 *  (see LineAsyncSink.h). Code that only writes log lines should include the
 *  lightweight LogStream.h instead; BCOS_LOG and the LOG_* macros live there.
 *
 * @file: BoostLog.h
 * @author: yujiechen
 * @date 2021-02-24
 */
#pragma once

#include "LogStream.h"
#include <string>

namespace bcos
{
extern std::string const FileLogger;
}  // namespace bcos
