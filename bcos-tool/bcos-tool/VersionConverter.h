
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
#include <bcos-framework/protocol/Protocol.h>
#include <string>
#include <vector>

namespace bcos::tool
{
inline uint32_t toVersionNumber(const std::string& _version)
{
    auto version = _version;
    boost::to_lower(version);
    // 3.0.0-rc{x} version
    if (_version.starts_with(bcos::protocol::RC_VERSION_PREFIX))
    {
        std::string versionNumber = _version.substr(bcos::protocol::RC_VERSION_PREFIX.length());
        return boost::lexical_cast<uint32_t>(versionNumber);
    }
    std::vector<std::string> versionFields;
    boost::split(versionFields, version, boost::is_any_of("."));
    if (versionFields.size() < 2)
    {
        BOOST_THROW_EXCEPTION(InvalidVersion() << errinfo_comment(
                                  "The version must be in format of MAJOR.MINOR.PATCH, "
                                  "and the PATCH version is optional, current version is " +
                                  _version));
    }
    try
    {
        // MAJOR.MINOR.PATCH 0xMMmmpp00 every field is uint8_t, last byte is reserved
        // v3.1.1 => 0x03010100
        auto majorVersion = boost::lexical_cast<uint32_t>(versionFields[0]);
        auto minorVersion = boost::lexical_cast<uint32_t>(versionFields[1]);
        auto patchVersion = 0;
        if (versionFields.size() == 3)
        {
            patchVersion = boost::lexical_cast<uint32_t>(versionFields[2]);
        }
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
        return (majorVersion << 24) + (minorVersion << 16) + (patchVersion << 8);
    }
    catch (const boost::bad_lexical_cast& e)
    {
        BOOST_THROW_EXCEPTION(InvalidVersion() << errinfo_comment(_version));
    }
}
}  // namespace bcos::tool