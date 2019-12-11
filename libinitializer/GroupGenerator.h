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

}  // namespace initializer
}  // namespace dev