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
 * @brief: define Log
 *
 * @file: Log.cpp
 * @author: yujiechen
 * @date 2021-02-24
 * @author: xingqiangbai
 * @date 2024-01-18
 * @brief: add file collector
 */
#include "GzTools.h"
#include "Log.h"
#include <boost/date_time/time_facet.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/intrusive/link_mode.hpp>
#include <boost/intrusive/list.hpp>
#include <boost/intrusive/list_hook.hpp>
#include <boost/log/attributes/time_traits.hpp>
#include <boost/log/core.hpp>
#include <boost/log/detail/singleton.hpp>
#include <boost/make_shared.hpp>
#include <boost/spirit/home/qi/numeric/numeric_utils.hpp>
namespace bcos
{
std::string const FileLogger = "FileLogger";
boost::log::sources::severity_channel_logger_mt<boost::log::trivial::severity_level, std::string>
    FileLoggerHandler(boost::log::keywords::channel = FileLogger);

std::string const StatFileLogger = "StatFileLogger";
boost::log::sources::severity_channel_logger_mt<boost::log::trivial::severity_level, std::string>
    StatFileLoggerHandler(boost::log::keywords::channel = StatFileLogger);

LogLevel c_fileLogLevel = LogLevel::TRACE;
LogLevel c_statLogLevel = LogLevel::INFO;

void setFileLogLevel(LogLevel const& _level)
{
    c_fileLogLevel = _level;
}

void setStatLogLevel(LogLevel const& _level)
{
    c_statLogLevel = _level;
}

namespace
{  // copy form
using path_string_type = boost::filesystem::path::string_type;
using path_char_type = path_string_type::value_type;
using filesystem_error = boost::filesystem::filesystem_error;
using path_string_type = boost::filesystem::path::string_type;
using path_char_type = path_string_type::value_type;

//! An auxiliary traits that contain various constants and functions regarding string and character
//! operations
template <typename CharT>
struct file_char_traits;

template <>
struct file_char_traits<char>
{
    using char_type = char;

    static const char_type percent = '%';
    static const char_type number_placeholder = 'N';
    static const char_type day_placeholder = 'd';
    static const char_type month_placeholder = 'm';
    static const char_type year_placeholder = 'y';
    static const char_type full_year_placeholder = 'Y';
    static const char_type frac_sec_placeholder = 'f';
    static const char_type seconds_placeholder = 'S';
    static const char_type minutes_placeholder = 'M';
    static const char_type hours_placeholder = 'H';
    static const char_type space = ' ';
    static const char_type plus = '+';
    static const char_type minus = '-';
    static const char_type zero = '0';
    static const char_type dot = '.';
    static const char_type newline = '\n';

    static bool is_digit(char c)
    {
        using namespace std;
        return (isdigit(c) != 0);
    }
    static std::string default_file_name_pattern() { return "%5N.log"; }
};

#ifndef BOOST_LOG_BROKEN_STATIC_CONSTANTS_LINKAGE
const file_char_traits<char>::char_type file_char_traits<char>::percent;
const file_char_traits<char>::char_type file_char_traits<char>::number_placeholder;
const file_char_traits<char>::char_type file_char_traits<char>::day_placeholder;
const file_char_traits<char>::char_type file_char_traits<char>::month_placeholder;
const file_char_traits<char>::char_type file_char_traits<char>::year_placeholder;
const file_char_traits<char>::char_type file_char_traits<char>::full_year_placeholder;
const file_char_traits<char>::char_type file_char_traits<char>::frac_sec_placeholder;
const file_char_traits<char>::char_type file_char_traits<char>::seconds_placeholder;
const file_char_traits<char>::char_type file_char_traits<char>::minutes_placeholder;
const file_char_traits<char>::char_type file_char_traits<char>::hours_placeholder;
const file_char_traits<char>::char_type file_char_traits<char>::space;
const file_char_traits<char>::char_type file_char_traits<char>::plus;
const file_char_traits<char>::char_type file_char_traits<char>::minus;
const file_char_traits<char>::char_type file_char_traits<char>::zero;
const file_char_traits<char>::char_type file_char_traits<char>::dot;
const file_char_traits<char>::char_type file_char_traits<char>::newline;
#endif  // BOOST_LOG_BROKEN_STATIC_CONSTANTS_LINKAGE

template <>
struct file_char_traits<wchar_t>
{
    using char_type = wchar_t;

    static const char_type percent = L'%';
    static const char_type number_placeholder = L'N';
    static const char_type day_placeholder = L'd';
    static const char_type month_placeholder = L'm';
    static const char_type year_placeholder = L'y';
    static const char_type full_year_placeholder = L'Y';
    static const char_type frac_sec_placeholder = L'f';
    static const char_type seconds_placeholder = L'S';
    static const char_type minutes_placeholder = L'M';
    static const char_type hours_placeholder = L'H';
    static const char_type space = L' ';
    static const char_type plus = L'+';
    static const char_type minus = L'-';
    static const char_type zero = L'0';
    static const char_type dot = L'.';
    static const char_type newline = L'\n';

    static bool is_digit(wchar_t c)
    {
        using namespace std;
        return (iswdigit(c) != 0);
    }
    static std::wstring default_file_name_pattern() { return L"%5N.log"; }
};

#ifndef BOOST_LOG_BROKEN_STATIC_CONSTANTS_LINKAGE
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::percent;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::number_placeholder;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::day_placeholder;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::month_placeholder;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::year_placeholder;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::full_year_placeholder;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::frac_sec_placeholder;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::seconds_placeholder;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::minutes_placeholder;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::hours_placeholder;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::space;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::plus;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::minus;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::zero;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::dot;
const file_char_traits<wchar_t>::char_type file_char_traits<wchar_t>::newline;
#endif  // BOOST_LOG_BROKEN_STATIC_CONSTANTS_LINKAGE

//! The functor formats the file counter into the file name
class file_counter_formatter
{
public:
    using result_type = path_string_type;

private:
    //! The position in the pattern where the file counter placeholder is
    path_string_type::size_type m_FileCounterPosition;
    std::streamsize m_Width;                                    //! File counter width
    mutable std::basic_ostringstream<path_char_type> m_Stream;  //! File counter formatting stream

public:
    //! Initializing constructor
    file_counter_formatter(path_string_type::size_type pos, unsigned int width)
      : m_FileCounterPosition(pos), m_Width(width)
    {
        typedef file_char_traits<path_char_type> traits_t;
        m_Stream.fill(traits_t::zero);
    }
    //! Copy constructor
    file_counter_formatter(file_counter_formatter const& that)
      : m_FileCounterPosition(that.m_FileCounterPosition), m_Width(that.m_Width)
    {
        m_Stream.fill(that.m_Stream.fill());
    }
    //! The function formats the file counter into the file name
    path_string_type operator()(path_string_type const& pattern, unsigned int counter) const
    {
        path_string_type file_name = pattern;

        m_Stream.str(path_string_type());
        m_Stream.width(m_Width);
        m_Stream << counter;
        file_name.insert(m_FileCounterPosition, m_Stream.str());

        return file_name;
    }
    BOOST_DELETED_FUNCTION(file_counter_formatter& operator=(file_counter_formatter const&))
};

//! The function parses the format placeholder for file counter
bool parse_counter_placeholder(
    path_string_type::const_iterator& it, path_string_type::const_iterator end, unsigned int& width)
{
    typedef boost::spirit::qi::extract_uint<unsigned int, 10, 1, -1> width_extract;
    typedef file_char_traits<path_char_type> traits_t;
    if (it == end)
    {
        return false;
    }
    path_char_type c = *it;
    if (c == traits_t::zero || c == traits_t::space || c == traits_t::plus || c == traits_t::minus)
    {
        // Skip filler and alignment specification
        ++it;
        if (it == end)
        {
            return false;
        }
        c = *it;
    }
    if (traits_t::is_digit(c))
    {
        // Parse width
        if (!width_extract::call(it, end, width))
        {
            return false;
        }
        if (it == end)
        {
            return false;
        }
        c = *it;
    }
    if (c == traits_t::dot)
    {
        // Skip precision
        ++it;
        while (it != end && traits_t::is_digit(*it))
        {
            ++it;
        }
        if (it == end)
        {
            return false;
        }
        c = *it;
    }
    if (c == traits_t::number_placeholder)
    {
        ++it;
        return true;
    }
    return false;
}

//! The function matches the file name and the pattern
bool match_pattern(path_string_type const& file_name, path_string_type const& pattern,
    unsigned int& file_counter, bool& file_counter_parsed)
{
    typedef boost::spirit::qi::extract_uint<unsigned int, 10, 1, -1> file_counter_extract;
    typedef file_char_traits<path_char_type> traits_t;

    struct local
    {  // Verifies that the string contains exactly n digits
        static bool scan_digits(path_string_type::const_iterator& it,
            path_string_type::const_iterator end, std::ptrdiff_t n)
        {
            for (; n > 0; --n)
            {
                if (it == end)
                {
                    return false;
                }
                path_char_type c = *it++;
                if (!traits_t::is_digit(c))
                {
                    return false;
                }
            }
            return true;
        }
    };

    path_string_type::const_iterator f_it = file_name.begin();
    path_string_type::const_iterator f_end = file_name.end();
    path_string_type::const_iterator p_it = pattern.begin();
    path_string_type::const_iterator p_end = pattern.end();
    bool placeholder_expected = false;
    while (f_it != f_end && p_it != p_end)
    {
        path_char_type p_c = *p_it;
        path_char_type f_c = *f_it;
        if (!placeholder_expected)
        {
            if (p_c == traits_t::percent)
            {
                placeholder_expected = true;
                ++p_it;
            }
            else if (p_c == f_c)
            {
                ++p_it;
                ++f_it;
            }
            else
            {
                return false;
            }
        }
        else
        {
            switch (p_c)
            {
            case traits_t::percent:  // An escaped '%'
                if (p_c == f_c)
                {
                    ++p_it;
                    ++f_it;
                    break;
                }
                else
                {
                    return false;
                }
            case traits_t::seconds_placeholder:  // Date/time components with 2-digits width
            case traits_t::minutes_placeholder:
            case traits_t::hours_placeholder:
            case traits_t::day_placeholder:
            case traits_t::month_placeholder:
            case traits_t::year_placeholder:
                if (!local::scan_digits(f_it, f_end, 2))
                {
                    return false;
                }
                ++p_it;
                break;

            case traits_t::full_year_placeholder:  // Date/time components with 4-digits width
                if (!local::scan_digits(f_it, f_end, 4))
                {
                    return false;
                }
                ++p_it;
                break;

            case traits_t::frac_sec_placeholder:  // Fraction seconds width is
                                                  // configuration-dependent
                typedef boost::posix_time::time_res_traits posix_resolution_traits;
                if (!local::scan_digits(
                        f_it, f_end, posix_resolution_traits::num_fractional_digits()))
                {
                    return false;
                }
                ++p_it;
                break;

            default:  // This should be the file counter placeholder or some unsupported placeholder
            {
                path_string_type::const_iterator p = p_it;
                unsigned int width = 0;
                if (!parse_counter_placeholder(p, p_end, width))
                {
                    BOOST_THROW_EXCEPTION(std::invalid_argument(
                        "Unsupported placeholder used in pattern for file scanning"));
                }
                // Find where the file number ends
                path_string_type::const_iterator f = f_it;
                if (!local::scan_digits(f, f_end, width))
                {
                    return false;
                }
                while (f != f_end && traits_t::is_digit(*f))
                {
                    ++f;
                }

                if (!file_counter_extract::call(f_it, f, file_counter))
                {
                    return false;
                }

                file_counter_parsed = true;
                p_it = p;
            }
            break;
            }

            placeholder_expected = false;
        }
    }
    if (p_it == p_end)
    {
        if (f_it != f_end)
        {  // The actual file name may end with an additional counter
            // that is added by the collector in case if file name clash
            return local::scan_digits(f_it, f_end, std::distance(f_it, f_end));
        }
        return true;
    }
    return false;
}

//! A possible Boost.Filesystem extension - renames or moves the file to the target storage
inline void move_file(boost::filesystem::path const& from, boost::filesystem::path const& to)
{
#if defined(BOOST_WINDOWS_API)
    // On Windows MoveFile already does what we need
    boost::filesystem::rename(from, to);
#else
    // On POSIX rename fails if the target points to a different device

    boost::system::error_code ec;
    boost::filesystem::rename(from, to, ec);
    if (ec)
    {
        if (BOOST_LIKELY(ec.value() == boost::system::errc::cross_device_link))
        {
            // Attempt to manually move the file instead
            boost::filesystem::copy_file(from, to);
            boost::filesystem::remove(from);
        }
        else
        {
            BOOST_THROW_EXCEPTION(
                filesystem_error("failed to move file to another location", from, to, ec));
        }
    }
#endif
}

inline void convert_to_tar_gz(
    boost::filesystem::path const& from, boost::filesystem::path const& to)
{
    bcos::createGzFile(from.string(), to.string());
    boost::filesystem::remove(from);
}

class file_collector_repository;

//! Type of the hook used for sequencing file collectors
using file_collector_hook =
    boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::safe_link>>;

}  // namespace
}  // namespace bcos
