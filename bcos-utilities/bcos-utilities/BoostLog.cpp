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

//! Log file collector implementation
class file_collector : public boost::log::sinks::file::collector,
                       public file_collector_hook,
                       public boost::enable_shared_from_this<file_collector>
{
private:
    //! Information about a single stored file
    struct file_info
    {
        //! Ordering predicate by timestamp
        struct order_by_timestamp
        {
            using result_type = bool;

            result_type operator()(
                file_info const& left, file_info const& right) const BOOST_NOEXCEPT
            {
                return left.m_TimeStamp < right.m_TimeStamp;
            }
        };

        //! Predicate for testing if a file_info refers to a file equivalent to another path
        class equivalent_file
        {
        public:
            using result_type = bool;
            explicit equivalent_file(boost::filesystem::path const& path) BOOST_NOEXCEPT
              : m_Path(path)
            {}

            result_type operator()(file_info const& info) const
            {
                return boost::filesystem::equivalent(info.m_Path, m_Path);
            }

        private:
            boost::filesystem::path const& m_Path;
        };

        uintmax_t m_Size;
        std::time_t m_TimeStamp;
        boost::filesystem::path m_Path;
    };
    //! A list of the stored files
    using file_list = std::list<file_info>;
    //! The string type compatible with the universal path type
    using path_string_type = boost::filesystem::path::string_type;

    //! A reference to the repository this collector belongs to
    boost::shared_ptr<file_collector_repository> m_pRepository;

#if !defined(BOOST_LOG_NO_THREADS)
    //! Synchronization mutex
    std::mutex m_Mutex;
#endif  // !defined(BOOST_LOG_NO_THREADS)

    //! Total file size upper limit
    uintmax_t m_MaxSize;
    //! Free space lower limit
    uintmax_t m_MinFreeSpace;
    //! File count upper limit
    uintmax_t m_MaxFiles;

    //! The current path at the point when the collector is created
    /*
     * The special member is required to calculate absolute paths with no
     * dependency on the current path for the application, which may change
     */
    const boost::filesystem::path m_BasePath;
    //! Target directory to store files to
    boost::filesystem::path m_StorageDir;

    //! The list of stored files
    file_list m_Files;
    //! Total size of the stored files
    uintmax_t m_TotalSize;
    bool m_ConvertTarGZ = false;

public:
    //! Constructor
    file_collector(boost::shared_ptr<file_collector_repository> const& repo,
        boost::filesystem::path const& target_dir, uintmax_t max_size, uintmax_t min_free_space,
        uintmax_t max_files);

    //! Destructor
    ~file_collector() BOOST_OVERRIDE;

    //! The function stores the specified file in the storage
    void store_file(boost::filesystem::path const& src_path) BOOST_OVERRIDE;

    //! The function checks if the specified path refers to an existing file in the storage
    bool is_in_storage(boost::filesystem::path const& src_path) const BOOST_OVERRIDE;

    //! Scans the target directory for the files that have already been stored
    boost::log::sinks::file::scan_result scan_for_files(boost::log::sinks::file::scan_method method,
        boost::filesystem::path const& pattern) BOOST_OVERRIDE;

    //! The function updates storage restrictions
    void update(uintmax_t max_size, uintmax_t min_free_space, uintmax_t max_files);

    //! The function checks if the directory is governed by this collector
    bool is_governed(boost::filesystem::path const& dir) const
    {
        return boost::filesystem::equivalent(m_StorageDir, dir);
    }

    void convert_tar_gz(bool ConvertTarGZ)
    {
        m_ConvertTarGZ = ConvertTarGZ;
    }

private:
    //! Makes relative path absolute with respect to the base path
    boost::filesystem::path make_absolute(boost::filesystem::path const& path) const
    {
        return boost::filesystem::absolute(path, m_BasePath);
    }
    //! Acquires file name string from the path
    static path_string_type filename_string(boost::filesystem::path const& path)
    {
        return path.filename().string<path_string_type>();
    }
};


//! The singleton of the list of file collectors
class file_collector_repository : public boost::log::aux::lazy_singleton<file_collector_repository,
                                      boost::shared_ptr<file_collector_repository>>
{
private:
    //! Base type
    using base_type = boost::log::aux::lazy_singleton<file_collector_repository,
        boost::shared_ptr<file_collector_repository>>;

#if !defined(BOOST_LOG_BROKEN_FRIEND_TEMPLATE_SPECIALIZATIONS)
    friend class boost::log::aux::lazy_singleton<file_collector_repository,
        boost::shared_ptr<file_collector_repository>>;
#else
    friend class base_type;
#endif

    //! The type of the list of collectors
    using file_collectors =
        boost::intrusive::list<file_collector, boost::intrusive::base_hook<file_collector_hook>>;

#if !defined(BOOST_LOG_NO_THREADS)
    //! Synchronization mutex
    std::mutex m_Mutex;
#endif  // !defined(BOOST_LOG_NO_THREADS)
        //! The list of file collectors
    file_collectors m_Collectors;

public:
    //! Finds or creates a file collector
    boost::shared_ptr<boost::log::sinks::file::collector> get_collector(
        boost::filesystem::path const& target_dir, uintmax_t max_size, uintmax_t min_free_space,
        uintmax_t max_files, bool convert_tar_gz);

    //! Removes the file collector from the list
    void remove_collector(file_collector* fileCollector);

private:
    //! Initializes the singleton instance
    static void init_instance()
    {
        base_type::get_instance() = boost::make_shared<file_collector_repository>();
    }
};

//! Constructor
file_collector::file_collector(boost::shared_ptr<file_collector_repository> const& repo,
    boost::filesystem::path const& target_dir, uintmax_t max_size, uintmax_t min_free_space,
    uintmax_t max_files)
  : m_pRepository(repo),
    m_MaxSize(max_size),
    m_MinFreeSpace(min_free_space),
    m_MaxFiles(max_files),
    m_BasePath(boost::filesystem::current_path()),
    m_TotalSize(0)
{
    m_StorageDir = make_absolute(target_dir);
    boost::filesystem::create_directories(m_StorageDir);
}

//! Destructor
file_collector::~file_collector()
{
    m_pRepository->remove_collector(this);
}

//! The function stores the specified file in the storage
void file_collector::store_file(boost::filesystem::path const& src_path)
{
    // NOTE FOR THE FOLLOWING CODE:
    // Avoid using Boost.Filesystem functions that would call path::codecvt(). store_file() can be
    // called at process termination, and the global codecvt facet can already be destroyed at this
    // point. https://svn.boost.org/trac/boost/ticket/8642

    // Let's construct the new file name
    file_info info;
    info.m_TimeStamp = boost::filesystem::last_write_time(src_path);
    info.m_Size = boost::filesystem::file_size(src_path);

    const boost::filesystem::path file_name_path = src_path.filename();
    path_string_type const& file_name = file_name_path.native();
    info.m_Path = m_StorageDir / file_name_path;

    // Check if the file is already in the target directory
    boost::filesystem::path src_dir =
        src_path.has_parent_path() ? boost::filesystem::system_complete(src_path.parent_path()) :
                                     m_BasePath;
    const bool is_in_target_dir = boost::filesystem::equivalent(src_dir, m_StorageDir);
    if (!is_in_target_dir)
    {
        if (boost::filesystem::exists(info.m_Path))
        {
            // If the file already exists, try to mangle the file name
            // to ensure there's no conflict. I'll need to make this customizable some day.
            file_counter_formatter formatter(file_name.size(), 5);
            unsigned int n = 0;
            while (true)
            {
                path_string_type alt_file_name = formatter(file_name, n);
                info.m_Path = m_StorageDir / boost::filesystem::path(alt_file_name);
                if (!boost::filesystem::exists(info.m_Path))
                {
                    break;
                }

                if (BOOST_UNLIKELY(n == (std::numeric_limits<unsigned int>::max)()))
                {
                    BOOST_THROW_EXCEPTION(boost::filesystem::filesystem_error(
                        "Target file exists and an unused fallback file name could not be found",
                        info.m_Path,
                        boost::system::error_code(
                            boost::system::errc::io_error, boost::system::generic_category())));
                }

                ++n;
            }
        }

        // The directory should have been created in constructor, but just in case it got deleted
        // since then...
        boost::filesystem::create_directories(m_StorageDir);
    }

    std::lock_guard<std::mutex> lock(m_Mutex);

    auto it = m_Files.begin();
    const auto end = m_Files.end();
    if (is_in_target_dir)
    {
        // If the sink writes log file into the target dir (is_in_target_dir == true), it is
        // possible that after scanning an old file entry refers to the file that is picked up by
        // the sink for writing. Later on, the sink attempts to store the file in the storage. At
        // best, this would result in duplicate file entries. At worst, if the storage limits
        // trigger a deletion and this file get deleted, we may have an entry that refers to no
        // actual file. In any case, the total size of files in the storage will be incorrect. Here
        // we work around this problem and simply remove the old file entry without removing the
        // file. The entry will be re-added to the list later.
        while (it != end)
        {
            boost::system::error_code ec;
            if (boost::filesystem::equivalent(it->m_Path, info.m_Path, ec))
            {
                m_TotalSize -= it->m_Size;
                m_Files.erase(it);
                break;
            }
            ++it;
        }

        it = m_Files.begin();
    }

    // Check if an old file should be erased
    uintmax_t free_space = m_MinFreeSpace != 0U ? boost::filesystem::space(m_StorageDir).available :
                                                  static_cast<uintmax_t>(0);
    while (it != end && (m_TotalSize + info.m_Size > m_MaxSize ||
                            ((m_MinFreeSpace != 0U) && m_MinFreeSpace > free_space) ||
                            m_MaxFiles <= m_Files.size()))
    {
        file_info& old_info = *it;
        boost::system::error_code ec;
        boost::filesystem::file_status status = boost::filesystem::status(old_info.m_Path, ec);

        if (status.type() == boost::filesystem::regular_file)
        {
            try
            {
                boost::filesystem::remove(old_info.m_Path);
                // Free space has to be queried as it may not increase equally
                // to the erased file size on compressed filesystems
                if (m_MinFreeSpace != 0U)
                {
                    free_space = boost::filesystem::space(m_StorageDir).available;
                }
                m_TotalSize -= old_info.m_Size;
                it = m_Files.erase(it);
            }
            catch (boost::system::system_error&)
            {
                // Can't erase the file. Maybe it's locked? Never mind...
                ++it;
            }
        }
        else
        {
            // If it's not a file or is absent, just remove it from the list
            m_TotalSize -= old_info.m_Size;
            it = m_Files.erase(it);
        }
    }

    if (!is_in_target_dir)
    {
        // Move/rename the file to the target storage
        move_file(src_path, info.m_Path);
    }
    if (m_ConvertTarGZ)
    {
        auto from = info.m_Path;
        info.m_Path = info.m_Path.string() + std::string(".gz");
        convert_to_tar_gz(from, info.m_Path);
        info.m_Size = boost::filesystem::file_size(info.m_Path);
    }

    m_Files.push_back(info);
    m_TotalSize += info.m_Size;
}

//! The function checks if the specified path refers to an existing file in the storage
bool file_collector::is_in_storage(boost::filesystem::path const& src_path) const
{
    const boost::filesystem::path file_name_path = src_path.filename();
    const boost::filesystem::path trg_path = m_StorageDir / file_name_path;

    // Check if the file is already in the target directory
    boost::system::error_code ec;
    boost::filesystem::path src_dir =
        src_path.has_parent_path() ?
            boost::filesystem::system_complete(src_path.parent_path(), ec) :
            m_BasePath;
    if (ec)
    {
        return false;
    }

    boost::filesystem::file_status status = boost::filesystem::status(trg_path, ec);
    if (ec || status.type() != boost::filesystem::regular_file)
    {
        return false;
    }
    bool equiv = boost::filesystem::equivalent(src_dir / file_name_path, trg_path, ec);
    if (ec)
    {
        return false;
    }

    return equiv;
}

//! Scans the target directory for the files that have already been stored
boost::log::sinks::file::scan_result file_collector::scan_for_files(
    boost::log::sinks::file::scan_method method, boost::filesystem::path const& pattern)
{
    boost::log::sinks::file::scan_result result;
    if (method != boost::log::sinks::file::no_scan)
    {
        boost::filesystem::path dir = m_StorageDir;
        path_string_type mask;
        if (method == boost::log::sinks::file::scan_matching)
        {
            if (m_ConvertTarGZ)
            {
                auto filename = pattern.filename();
                filename.append(".gz");
                mask = filename_string(filename);
            }
            else
            {
                mask = filename_string(pattern);
            }
            if (pattern.has_parent_path())
            {
                dir = make_absolute(pattern.parent_path());
            }
        }

        boost::system::error_code ec;
        boost::filesystem::file_status status = boost::filesystem::status(dir, ec);
        if (status.type() == boost::filesystem::directory_file)
        {
            std::lock_guard<std::mutex> lock(m_Mutex);

            file_list files;
            boost::filesystem::directory_iterator it(dir);
            boost::filesystem::directory_iterator end;
            uintmax_t total_size = 0U;
            for (; it != end; ++it)
            {
                boost::filesystem::directory_entry const& dir_entry = *it;
                file_info info;
                info.m_Path = dir_entry.path();
                status = dir_entry.status(ec);
                if (status.type() == boost::filesystem::regular_file)
                {
                    // Check that there are no duplicates in the resulting list
                    if (std::find_if(m_Files.begin(), m_Files.end(),
                            file_info::equivalent_file(info.m_Path)) == m_Files.end())
                    {
                        // Check that the file name matches the pattern
                        unsigned int file_number = 0U;
                        bool file_number_parsed = false;
                        if (method != boost::log::sinks::file::scan_matching ||
                            match_pattern(filename_string(info.m_Path), mask, file_number,
                                file_number_parsed))
                        {
                            info.m_Size = boost::filesystem::file_size(info.m_Path);
                            total_size += info.m_Size;
                            info.m_TimeStamp = boost::filesystem::last_write_time(info.m_Path);
                            files.push_back(info);
                            ++result.found_count;

                            // Test that the file_number >= result.last_file_counter accounting for
                            // the integer overflow
                            if (file_number_parsed &&
                                (!result.last_file_counter ||
                                    (file_number - *result.last_file_counter) <
                                        ((~0U) ^ ((~0U) >> 1U))))
                            {
                                result.last_file_counter = file_number;
                            }
                        }
                    }
                }
            }

            // Sort files chronologically
            m_Files.splice(m_Files.end(), files);
            m_TotalSize += total_size;
            m_Files.sort(file_info::order_by_timestamp());
        }
    }

    return result;
}

//! The function updates storage restrictions
void file_collector::update(uintmax_t max_size, uintmax_t min_free_space, uintmax_t max_files)
{
    std::lock_guard<std::mutex> lock(m_Mutex);

    m_MaxSize = (std::min)(m_MaxSize, max_size);
    m_MinFreeSpace = (std::max)(m_MinFreeSpace, min_free_space);
    m_MaxFiles = (std::min)(m_MaxFiles, max_files);
}


//! Finds or creates a file collector
boost::shared_ptr<boost::log::sinks::file::collector> file_collector_repository::get_collector(
    boost::filesystem::path const& target_dir, uintmax_t max_size, uintmax_t min_free_space,
    uintmax_t max_files, bool convert_tar_gz)
{
    std::lock_guard<std::mutex> lock(m_Mutex);

    file_collectors::iterator it = std::find_if(
        m_Collectors.begin(), m_Collectors.end(), [&target_dir](file_collector const& collector) {
            return collector.is_governed(target_dir);
        });
    boost::shared_ptr<file_collector> collector;
    if (it != m_Collectors.end())
    {
        try
        {
            // This may throw if the collector is being currently destroyed
            collector = it->shared_from_this();
            collector->update(max_size, min_free_space, max_files);
        }
        catch (std::bad_weak_ptr&)
        {}
    }

    if (!collector)
    {
        collector = boost::make_shared<file_collector>(
            file_collector_repository::get(), target_dir, max_size, min_free_space, max_files);
        m_Collectors.push_back(*collector);
    }
    collector->convert_tar_gz(convert_tar_gz);
    return collector;
}

//! Removes the file collector from the list
void file_collector_repository::remove_collector(file_collector* fileCollector)
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Collectors.erase(m_Collectors.iterator_to(*fileCollector));
}
}  // namespace
namespace log
{

boost::shared_ptr<boost::log::sinks::file::collector> make_collector(
    boost::filesystem::path const& target_dir, uintmax_t max_size, uintmax_t min_free_space,
    uintmax_t max_files = (std::numeric_limits<uintmax_t>::max)(), bool convert_tar_gz = false)
{
    return file_collector_repository::get()->get_collector(
        target_dir, max_size, min_free_space, max_files, convert_tar_gz);
}
}  // namespace log
}  // namespace bcos
