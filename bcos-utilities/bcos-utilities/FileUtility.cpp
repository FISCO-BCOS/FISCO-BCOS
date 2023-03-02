/*
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
 * @file FileUtility.cpp
 */

#include "FileUtility.h"
#include <stdio.h>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <cstdlib>
#include <fstream>
using namespace std;

namespace bcos
{
template <typename T>
inline std::shared_ptr<T> genericReadContents(boost::filesystem::path const& _file)
{
    std::shared_ptr<T> content = std::make_shared<T>();
    size_t const c_elementSize = sizeof(typename T::value_type);

    boost::filesystem::ifstream fileStream(_file, std::ifstream::binary);
    if (!fileStream)
    {
        return content;
    }
    fileStream.seekg(0, fileStream.end);
    streamoff length = fileStream.tellg();

    if (length == 0)
    {
        return content;
    }
    fileStream.seekg(0, fileStream.beg);
    content->resize((length + c_elementSize - 1) / c_elementSize);
    fileStream.read(const_cast<char*>(reinterpret_cast<char const*>(content->data())), length);
    return content;
}

std::shared_ptr<bytes> readContents(boost::filesystem::path const& _file)
{
    return genericReadContents<bytes>(_file);
}

std::shared_ptr<string> readContentsToString(boost::filesystem::path const& _file)
{
    return genericReadContents<string>(_file);
}

std::shared_ptr<std::vector<std::string>> readAllLinesFromFile(std::string const& _file)
{
    std::shared_ptr<std::vector<std::string>> lines = std::make_shared<std::vector<std::string>>();

    std::ifstream fileIn(_file);
    if (false == fileIn.is_open())
    {
        return lines;
    }

    while (false == fileIn.eof())
    {
        std::string line;
        std::getline(fileIn, line);

        lines->emplace_back(std::move(line));
    }
    fileIn.close();

    return lines;
}

template <typename T>
bool genericWriteContents(boost::filesystem::path const& _file, std::shared_ptr<T> _contents)
{
    boost::filesystem::ofstream fileStream(_file, std::ofstream::binary);
    if (!fileStream)
    {
        return false;
    }
    fileStream.write(reinterpret_cast<char*>(_contents->data()), _contents->size());
    return true;
}

bool writeContents(boost::filesystem::path const& _file, std::shared_ptr<bytes> _contents)
{
    return genericWriteContents<bytes>(_file, _contents);
}

bool writeContentsByString(
    boost::filesystem::path const& _file, std::shared_ptr<std::string> _contents)
{
    return genericWriteContents<string>(_file, _contents);
}

bool writeAllLinesToFile(std::string const& _file, std::shared_ptr<std::vector<std::string>> _lines)
{
    std::ofstream fileOut(_file);
    if (false == fileOut.is_open())
    {
        return false;
    }

    for (auto& line : *_lines)
    {
        fileOut << line << std::endl;
    }
    fileOut.close();

    return true;
}

}  // namespace bcos
