/*
        This file is part of cpp-ethereum.

        cpp-ethereum is free software: you can redistribute it and/or modify
        it under the terms of the GNU General Public License as published by
        the Free Software Foundation, either version 3 of the License, or
        (at your option) any later version.

        cpp-ethereum is distributed in the hope that it will be useful,
        but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
        GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
        along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/** @file FileSystem.cpp
 * @authors
 *	 Eric Lombrozo <elombrozo@gmail.com>
 *	 Gav Wood <i@gavwood.com>
 * @date 2014
 */

#include "FileSystem.h"
#include <pwd.h>
#include <boost/filesystem.hpp>
#include <iostream>
using namespace std;
using namespace dev;

namespace fs = boost::filesystem;
// static_assert(BOOST_VERSION >= 106400, "Wrong boost headers version");

// Should be written to only once during startup
static fs::path s_fiscoBcosDir;
static fs::path s_fiscoBcosIpcDir;

void dev::setDataDir(fs::path const& _dataDir)
{
    s_fiscoBcosDir = _dataDir;
}

void dev::setIpcPath(fs::path const& _ipcDir)
{
    s_fiscoBcosIpcDir = _ipcDir;
}

fs::path dev::getIpcPath()
{
    // Strip "geth.ipc" suffix if provided.
    if (s_fiscoBcosIpcDir.filename() == "geth.ipc")
        return s_fiscoBcosIpcDir.parent_path();
    else
        return s_fiscoBcosIpcDir;
}

fs::path dev::getDataDir(string _prefix)
{
    if (_prefix.empty())
        _prefix = "fisco-bcos-data";
    if (_prefix == "fisco-bcos-data" && !s_fiscoBcosDir.empty())
        return s_fiscoBcosDir;
    return getDefaultDataDir(_prefix);
}

/// get ledger dir
fs::path dev::getLedgerDir(std::string ledger_name, std::string data_dir)
{
    fs::path rootDir = getDataDir(data_dir);
    return rootDir / (ledger_name);
}

fs::path dev::getDefaultDataDir(string _prefix)
{
    if (_prefix.empty())
        _prefix = "fisco-bcos-data";
    if (_prefix[0] == '/')
        return _prefix;
    fs::path dataDirPath = fs::path(".");
    return dataDirPath / (_prefix);
}

fs::path dev::appendToFilename(fs::path const& _orig, string const& _suffix)
{
    if (_orig.filename() == fs::path(".") || _orig.filename() == fs::path(".."))
        return _orig / fs::path(_suffix);
    else
        return _orig.parent_path() / fs::path(_orig.filename().string() + _suffix);
}
