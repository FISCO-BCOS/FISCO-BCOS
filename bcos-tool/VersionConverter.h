
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
 * @brief Public function for version converter
 * @file VersionConverter.h
 * @author: yujiechen
 * @date 2021-05-19
 */
#pragma once
#include "Exceptions.h"
#include <bcos-framework/interfaces/protocol/Protocol.h>
#include <string>
#include <vector>

namespace bcos
{
namespace tool
{
inline bcos::protocol::Version toVersionNumber(const std::string& _version)
{
    auto version = _version;
    boost::to_lower(version);
    if (version == bcos::protocol::RC4_VERSION_STR)
    {
        return bcos::protocol::Version::RC4_VERSION;
    }
    std::vector<std::string> versionFields;
    boost::split(versionFields, version, boost::is_any_of("."));
    if (versionFields.size() <= 2)
    {
        BOOST_THROW_EXCEPTION(InvalidVersion() << errinfo_comment(_version));
    }
    try
    {
        uint32_t versionNumber;
        // major_version.middle_version
        // major_version:  1 bytes
        // middle_version: 3 bytes
        auto majorVersion = boost::lexical_cast<uint32_t>(versionFields[0]);
        auto middleVersion = boost::lexical_cast<uint32_t>(versionFields[1]);
        // check the major version
        if (majorVersion > bcos::protocol::MAX_MAJOR_VERSION ||
            majorVersion < bcos::protocol::MIN_MAJOR_VERSION)
        {
            BOOST_THROW_EXCEPTION(
                InvalidVersion() << errinfo_comment(
                    "The major version must between " +
                    std::to_string(bcos::protocol::MIN_MAJOR_VERSION) + " to " +
                    std::to_string(bcos::protocol::MAX_MAJOR_VERSION) + ", version:" + _version));
        }
        versionNumber = (majorVersion << 24) + middleVersion;
        return (bcos::protocol::Version)(versionNumber);
    }
    catch (const boost::bad_lexical_cast& e)
    {
        BOOST_THROW_EXCEPTION(InvalidVersion() << errinfo_comment(_version));
    }
}
}  // namespace tool
}  // namespace bcos