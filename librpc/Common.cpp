#include "Common.h"
#include <iostream>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/zlib.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/algorithm/string.hpp>

using namespace std;

// from https://stackoverflow.com/questions/4538586/how-to-compress-a-buffer-with-zlib
std::string dev::compress(const std::string& _data)
{
    boost::iostreams::filtering_streambuf<boost::iostreams::output> output_stream;
    output_stream.push(boost::iostreams::zlib_compressor());
    std::stringstream string_stream;
    output_stream.push(string_stream);
    boost::iostreams::copy(boost::iostreams::basic_array_source<char>(_data.c_str(),
                                                                      _data.size()), output_stream);
    return string_stream.str();
}

std::string dev::decompress(const std::string& _data)
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

// https://stackoverflow.com/questions/7053538/how-do-i-encode-a-string-to-base64-using-only-boost
std::string dev::base64Encode(const std::string& _data)
{
    using namespace boost::archive::iterators;
    using It = base64_from_binary<transform_width<std::string::const_iterator, 6, 8>>;
    auto tmp = std::string(It(std::begin(_data)), It(std::end(_data)));
    return tmp.append((3 - _data.size() % 3) % 3, '=');
}

std::string dev::base64Decode(const std::string& _data){
    using namespace boost::archive::iterators;
    using It = transform_width<binary_from_base64<std::string::const_iterator>, 8, 6>;
    return boost::algorithm::trim_right_copy_if(std::string(It(std::begin(_data)), It(std::end(_data))), [](char c) {
        return c == '\0';
    });
}
