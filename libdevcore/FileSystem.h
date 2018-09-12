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
/** @file FileSystem.h
 * @authors
 *	 Eric Lombrozo <elombrozo@gmail.com>
 *	 Gav Wood <i@gavwood.com>
 * @date 2014
 *
 * @author: yujiechen
 * @date: 2018-09-12
 * @modifications:
 *  1. modify implementation of getDataDir to adapt to fisco-bcos
 *  2. add getLedgerDir for multiple-ledgers support
 */

#pragma once

#include <boost/filesystem.hpp>
#include <string>

namespace dev
{
/// Sets the data dir for the default ("ethereum") prefix.
void setDataDir(boost::filesystem::path const& _dir);
/// @returns the path for user data.
boost::filesystem::path getDataDir(std::string _prefix = "fisco-bcos-data");
boost::filesystem::path getLedgerDir(
    std::string ledger_name, std::string data_dir = "fisco-bcos-data");
/// @returns the default path for user data, ignoring the one set by `setDataDir`.
boost::filesystem::path getDefaultDataDir(std::string _prefix = "fisco-bcos-data");
/// Sets the ipc socket dir
void setIpcPath(boost::filesystem::path const& _ipcPath);
/// @returns the ipc path (default is DataDir)
boost::filesystem::path getIpcPath();

/// @returns a new path whose file name is suffixed with the given suffix.
boost::filesystem::path appendToFilename(
    boost::filesystem::path const& _orig, std::string const& _suffix);

}  // namespace dev
