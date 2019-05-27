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
/** @file CommonIO.cpp
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 */

#include "CommonIO.h"
#include "Exceptions.h"
#include "FileSystem.h"
#include <stdio.h>
#include <termios.h>
#include <boost/filesystem.hpp>
#include <cstdlib>
#include <fstream>
#include <iostream>
using namespace std;
using namespace dev;

namespace fs = boost::filesystem;

namespace dev
{
namespace
{
void createDirectoryIfNotExistent(boost::filesystem::path const& _path)
{
    if (!fs::exists(_path))
    {
        fs::create_directories(_path);
        DEV_IGNORE_EXCEPTIONS(fs::permissions(_path, fs::owner_all));
    }
}

}  // namespace

template <typename _T>
inline _T contentsGeneric(boost::filesystem::path const& _file)
{
    _T ret;
    size_t const c_elementSize = sizeof(typename _T::value_type);
    boost::filesystem::ifstream is(_file, std::ifstream::binary);
    if (!is)
        return ret;

    // get length of file:
    is.seekg(0, is.end);
    streamoff length = is.tellg();
    if (length == 0)
        return ret;  // do not read empty file (MSVC does not like it)
    is.seekg(0, is.beg);

    ret.resize((length + c_elementSize - 1) / c_elementSize);
    is.read(const_cast<char*>(reinterpret_cast<char const*>(ret.data())), length);
    return ret;
}

bytes contents(boost::filesystem::path const& _file)
{
    return contentsGeneric<bytes>(_file);
}

bytesSec contentsSec(boost::filesystem::path const& _file)
{
    bytes b = contentsGeneric<bytes>(_file);
    bytesSec ret(b);
    bytesRef(&b).cleanse();
    return ret;
}

string contentsString(boost::filesystem::path const& _file)
{
    return contentsGeneric<string>(_file);
}

void writeFile(boost::filesystem::path const& _file, bytesConstRef _data, bool _writeDeleteRename)
{
    if (_writeDeleteRename)
    {
        fs::path tempPath =
            appendToFilename(_file, "-%%%%%%");  // XXX should not convert to string for this
        writeFile(tempPath, _data, false);
        // will delete _file if it exists
        fs::rename(tempPath, _file);
    }
    else
    {
        createDirectoryIfNotExistent(_file.parent_path());

        boost::filesystem::ofstream s(_file, ios::trunc | ios::binary);
        s.write(reinterpret_cast<char const*>(_data.data()), _data.size());
        if (!s)
            BOOST_THROW_EXCEPTION(
                FileError() << errinfo_comment("Could not write to file: " + _file.string()));
        DEV_IGNORE_EXCEPTIONS(fs::permissions(_file, fs::owner_read | fs::owner_write));
    }
}

void copyDirectory(boost::filesystem::path const& _srcDir, boost::filesystem::path const& _dstDir)
{
    createDirectoryIfNotExistent(_dstDir);

    for (fs::directory_iterator file(_srcDir); file != fs::directory_iterator(); ++file)
        fs::copy_file(file->path(), _dstDir / file->path().filename());
}
}  // namespace dev
