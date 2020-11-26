/**
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
 * along with FISCO-BCOS.  If not, see <http:www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 *
 * @file Common.cpp
 * @author: xingqiangbai
 * @date 2020-10-14
 */

#include "Common.h"
#include <boost/algorithm/string.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <iostream>

using namespace std;

// from https://stackoverflow.com/questions/4538586/how-to-compress-a-buffer-with-zlib
std::string bcos::compress(const std::string& _data)
{
    boost::iostreams::filtering_streambuf<boost::iostreams::output> output_stream;
    output_stream.push(boost::iostreams::zlib_compressor());
    std::stringstream string_stream;
    output_stream.push(string_stream);
    boost::iostreams::copy(
        boost::iostreams::basic_array_source<char>(_data.c_str(), _data.size()), output_stream);
    return string_stream.str();
}

std::string bcos::decompress(const std::string& _data)
{
    std::stringstream string_stream;
    string_stream << _data;
    boost::iostreams::filtering_streambuf<boost::iostreams::input> input_stream;
    input_stream.push(boost::iostreams::zlib_decompressor());

    input_stream.push(string_stream);
    std::stringstream unpacked_text;
    boost::iostreams::copy(input_stream, unpacked_text);
    return unpacked_text.str();
}
