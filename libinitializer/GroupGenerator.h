/*
 * @CopyRight:
 * FISCO-BCOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FISCO-BCOS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2019 fisco-dev contributors.
 */
/**
 * @brief : Generate group files
 * @author: jimmyshi
 * @date: 2019-12-11
 */

#pragma once

#include "Common.h"
#include <libconfig/GlobalConfigure.h>
#include <libdevcore/CommonData.h>
#include <libdevcore/Exceptions.h>
#include <libethcore/Protocol.h>
#include <string>

namespace dev
{
namespace initializer
{
class GenesisGenerator
{
public:
    static void generate(const std::string& _confDir, dev::GROUP_ID _groupId,
        const std::string& _timestamp, const std::set<std::string>& _sealerList);

private:
    static void check(std::string _filePath, dev::GROUP_ID _groupId, const std::string& _timestamp,
        const std::set<std::string>& _sealerList);
    static inline std::string fileName(dev::GROUP_ID _groupId)
    {
        return "group." + std::to_string(_groupId) + ".genesis";
    }
};

class GroupConfigGenerator
{
public:
    static void generate(const std::string& _confDir, dev::GROUP_ID _groupId);

private:
    static void check(std::string _filePath, dev::GROUP_ID _groupId);
    static inline std::string fileName(dev::GROUP_ID _groupId)
    {
        return "group." + std::to_string(_groupId) + ".ini";
    }
};

class GroupGenerator
{
public:
    static void generate(
        int _groupId, const std::string& _timestamp, const std::set<std::string>& _sealerList);

    static bool checkGroupID(int _groupId);
    static bool checkTimestamp(const std::string& _timestamp);
    static bool checkSealerList(const std::set<std::string>& _sealerList);
};

namespace GroupGeneratorCode
{
// success
const std::string SUCCESS = "0x0";
// group already exist
const std::string GROUP_EXIST = "0x1";
// group genesis file already exist
const std::string GROUP_GENESIS_FILE_EXIST = "0x2";
// group config file already exist
const std::string GROUP_CONFIG_FILE_EXIST = "0x3";
// invalid parameters
const std::string INVALID_PARAMS = "0x4";
// node inner error
const std::string OTHER_ERROR = "0x5";
}  // namespace GroupGeneratorCode


namespace GroupStarterCode
{
// success
const std::string SUCCESS = "0x0";
// group already running
const std::string GROUP_IS_RUNNING = "0x1";
// group genesis file not exist
const std::string GROUP_GENESIS_FILE_NOT_EXIST = "0x2";
// group config file not exist
const std::string GROUP_CONFIG_FILE_NOT_EXIST = "0x3";
// group genesis file invalid
const std::string GROUP_GENESIS_INVALID = "0x4";
// group config file invalid
const std::string GROUP_CONFIG_INVALID = "0x5";
// invalid parameters
const std::string INVALID_PARAMS = "0x6";
// node inner error
const std::string OTHER_ERROR = "0x7";
}  // namespace GroupStarterCode

}  // namespace initializer
}  // namespace dev