/*
    This file is part of FISCO-BCOS.

    FISCO-BCOS is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    FISCO-BCOS is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with FISCO-BCOS. If not, see <http://www.gnu.org/licenses/>.
*/
/** @file Common.h
 * @author Gav Wood <i@gavwood.com>
 * @date 2014
 *
 * Very common stuff (i.e. that every other header needs except vector_ref.h).
 *
 * @author wheatli
 * @date 2018.8.27
 * @modify add owning_bytes_ref
 *
 * @author yujiechen
 * @date 2018.9.5
 * @modify: remove useless micro-definition 'DEV_IF_THROWS'
 *          remove useless functions: toLog2, inUnits
 */

#pragma once

// way too many unsigned to size_t warnings in 32 bit build
#ifdef _M_IX86
#pragma warning(disable : 4244)
#endif

#ifdef __INTEL_COMPILER
#pragma warning(disable : 3682)  // call through incomplete class
#endif
#define DEV_QUOTED_HELPER(s) #s
#define DEV_QUOTED(s) DEV_QUOTED_HELPER(s)

#include <sys/time.h>
#include <chrono>
#include <functional>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#pragma warning(push)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include <boost/multiprecision/cpp_int.hpp>
#pragma warning(pop)
#pragma GCC diagnostic pop
#include "secure_vector.h"
#include "vector_ref.h"

// CryptoPP defines byte in the global namespace, so must we.
using byte = uint8_t;

// catch exceptions
#define DEV_IGNORE_EXCEPTIONS(X) \
    try                          \
    {                            \
        X;                       \
    }                            \
    catch (...)                  \
    {                            \
    }

namespace dev
{
using namespace boost::multiprecision::literals;

extern std::string const EmptyString;

//------ Common Type Definitions--------------
// vector of byte data
using bytes = std::vector<byte>;
using bytesRef = vector_ref<byte>;
using bytesConstRef = vector_ref<byte const>;
// manage vector securely in memory
using bytesSec = secure_vector<byte>;

// Numeric types.
using bigint = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<>>;
// unsigned int64
using u64 = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<64, 64,
    boost::multiprecision::unsigned_magnitude, boost::multiprecision::unchecked, void>>;
// unsigned int128
using u128 = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<128, 128,
    boost::multiprecision::unsigned_magnitude, boost::multiprecision::unchecked, void>>;
// unsigned int256
using u256 = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<256, 256,
    boost::multiprecision::unsigned_magnitude, boost::multiprecision::unchecked, void>>;
// signed int256
using s256 = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<256, 256,
    boost::multiprecision::signed_magnitude, boost::multiprecision::unchecked, void>>;
// unsigned int160
using u160 = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<160, 160,
    boost::multiprecision::unsigned_magnitude, boost::multiprecision::unchecked, void>>;
// signed int160
using s160 = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<160, 160,
    boost::multiprecision::signed_magnitude, boost::multiprecision::unchecked, void>>;
// unsigned int256
using u512 = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<512, 512,
    boost::multiprecision::unsigned_magnitude, boost::multiprecision::unchecked, void>>;
// signed int256
using s512 = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<512, 512,
    boost::multiprecision::signed_magnitude, boost::multiprecision::unchecked, void>>;
using u256s = std::vector<u256>;  // vector with unsigned int256 elements
using u160s = std::vector<u160>;  // vector with unsigned int160 elements
using u256Set = std::set<u256>;   // set with unsigned int256 elements
using u160Set = std::set<u160>;   // set with unsigned int160 elements

// Map types.
using StringMap = std::map<std::string, std::string>;
using BytesMap = std::map<bytes, bytes>;
using u256Map = std::map<u256, u256>;
using HexMap = std::map<bytes, bytes>;

// Hash types.
using StringHashMap = std::unordered_map<std::string, std::string>;
using u256HashMap = std::unordered_map<u256, u256>;

// String types.
using strings = std::vector<std::string>;
using string64 = std::array<char, 64>;
// Fixed-length string types.
using string32 = std::array<char, 32>;

// Null/Invalid values for convenience.
extern bytes const NullBytes;
u256 constexpr Invalid256 =
    0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff_cppui256;

//------------ Type interprets and Convertions----------------
/// Interprets @a _u as a two's complement signed number and returns the resulting s256.
inline s256 u2s(u256 _u)
{
    static const bigint c_end = bigint(1) << 256;
    /// get the +/- symbols
    if (boost::multiprecision::bit_test(_u, 255))
        return s256(-(c_end - _u));
    else
        return s256(_u);
}

/// @returns the two's complement signed representation of the signed number _u.
inline u256 s2u(s256 _u)
{
    static const bigint c_end = bigint(1) << 256;
    if (_u >= 0)
        return u256(_u);
    else
        return u256(c_end + _u);
}

inline int stringCmpIgnoreCase(const std::string& lhs, const std::string& rhs)
{
#if defined(_WIN32)
    return _stricmp(lhs.c_str(), rhs.c_str());
#else
    return strcasecmp(lhs.c_str(), rhs.c_str());
#endif
}

//-----------Common Convertions and Calcultations--------------------

template <size_t n>
inline u256 exp10()
{
    return exp10<n - 1>() * u256(10);
}

template <>
inline u256 exp10<0>()
{
    return u256(1);
}

/// @returns the absolute distance between _a and _b.
template <class N>
inline N diff(N const& _a, N const& _b)
{
    return std::max(_a, _b) - std::min(_a, _b);
}

/// RAII utility class whose destructor calls a given function.
class ScopeGuard
{
public:
    ScopeGuard(std::function<void(void)> _f) : m_f(_f) {}
    ~ScopeGuard() { m_f(); }

private:
    std::function<void(void)> m_f;
};

/// Inheritable for classes that have invariants.
class HasInvariants
{
public:
    /// Reimplement to specify the invariants.
    virtual bool invariants() const = 0;
};

/// RAII checker for invariant assertions.
class InvariantChecker
{
public:
    InvariantChecker(HasInvariants* _this, char const* _fn, char const* _file, int _line)
      : m_this(_this), m_function(_fn), m_file(_file), m_line(_line)
    {
        checkInvariants(_this, _fn, _file, _line, true);
    }
    ~InvariantChecker() { checkInvariants(m_this, m_function, m_file, m_line, false); }
    /// Check invariants are met, throw if not.
    static void checkInvariants(
        HasInvariants const* _this, char const* _fn, char const* _file, int line, bool _pre);

private:
    HasInvariants const* m_this;
    char const* m_function;
    char const* m_file;
    int m_line;
};

/// Scope guard for invariant check in a class derived from HasInvariants.
#if ETH_DEBUG
#define DEV_INVARIANT_CHECK \
    ::dev::InvariantChecker __dev_invariantCheck(this, BOOST_CURRENT_FUNCTION, __FILE__, __LINE__)
#define DEV_INVARIANT_CHECK_HERE \
    ::dev::InvariantChecker::checkInvariants(this, BOOST_CURRENT_FUNCTION, __FILE__, __LINE__, true)
#else
#define DEV_INVARIANT_CHECK (void)0;
#define DEV_INVARIANT_CHECK_HERE (void)0;
#endif

/// Simple scope-based timer helper.
class TimerHelper
{
public:
    TimerHelper(std::string const& _id, unsigned _msReportWhenGreater = 0)
      : m_t(std::chrono::high_resolution_clock::now()), m_id(_id), m_ms(_msReportWhenGreater)
    {}
    ~TimerHelper();

private:
    std::chrono::high_resolution_clock::time_point m_t;
    std::string m_id;
    unsigned m_ms;
};

class Timer
{
public:
    Timer() { restart(); }

    std::chrono::high_resolution_clock::duration duration() const
    {
        return std::chrono::high_resolution_clock::now() - m_t;
    }
    /// return seconds
    double elapsed() const
    {
        return std::chrono::duration_cast<std::chrono::microseconds>(duration()).count() /
               1000000.0;
    }
    void restart() { m_t = std::chrono::high_resolution_clock::now(); }

private:
    std::chrono::high_resolution_clock::time_point m_t;
};

#define DEV_TIMED(S)                                                             \
    for (::std::pair<::dev::TimerHelper, bool> __eth_t(S, true); __eth_t.second; \
         __eth_t.second = false)
#define DEV_TIMED_SCOPE(S) ::dev::TimerHelper __eth_t(S)
#define DEV_TIMED_FUNCTION DEV_TIMED_SCOPE(__PRETTY_FUNCTION__)

#define DEV_TIMED_ABOVE(S, MS)                                                           \
    for (::std::pair<::dev::TimerHelper, bool> __eth_t(::dev::TimerHelper(S, MS), true); \
         __eth_t.second; __eth_t.second = false)
#define DEV_TIMED_SCOPE_ABOVE(S, MS) ::dev::TimerHelper __eth_t(S, MS)
#define DEV_TIMED_FUNCTION_ABOVE(MS) DEV_TIMED_SCOPE_ABOVE(__PRETTY_FUNCTION__, MS)

#define DEV_UNUSED __attribute__((unused))

enum class WithExisting : int
{
    Trust = 0,
    Verify,
    Rescue,
    Kill
};

/// Get the current time in seconds since the epoch in UTC(ms)
uint64_t utcTime();

/// Reference to a slice of buffer that also owns the buffer.
///
/// This is extension to the concept C++ STL library names as array_view
/// (also known as gsl::span, array_ref, here vector_ref) -- reference to
/// continuous non-modifiable memory. The extension makes the object also owning
/// the referenced buffer.
///
/// This type is used by VMs to return output coming from RETURN instruction.
/// To avoid memory copy, a VM returns its whole memory + the information what
/// part of this memory is actually the output. This simplifies the VM design,
/// because there are multiple options how the output will be used (can be
/// ignored, part of it copied, or all of it copied). The decision what to do
/// with it was moved out of VM interface making VMs "stateless".
///
/// The type is movable, but not copyable. Default constructor available.
class owning_bytes_ref : public vector_ref<byte const>
{
public:
    owning_bytes_ref() = default;

    /// @param _bytes  The buffer.
    /// @param _begin  The index of the first referenced byte.
    /// @param _size   The number of referenced bytes.
    owning_bytes_ref(bytes&& _bytes, size_t _begin, size_t _size) : m_bytes(std::move(_bytes))
    {
        // Set the reference *after* the buffer is moved to avoid
        // pointer invalidation.
        retarget(&m_bytes[_begin], _size);
    }

    owning_bytes_ref(owning_bytes_ref const&) = delete;
    owning_bytes_ref(owning_bytes_ref&&) = default;
    owning_bytes_ref& operator=(owning_bytes_ref const&) = delete;
    owning_bytes_ref& operator=(owning_bytes_ref&&) = default;

    /// Moves the bytes vector out of here. The object cannot be used any more.
    bytes&& takeBytes()
    {
        reset();  // Reset reference just in case.
        return std::move(m_bytes);
    }

private:
    bytes m_bytes;
};

template <class T>
class QueueSet
{
public:
    bool push(T const& _t)
    {
        if (m_set.count(_t) == 0)
        {
            m_set.insert(_t);
            m_queue.push(_t);
            return true;
        }
        return false;
    }
    bool pop()
    {
        if (m_queue.size() == 0)
            return false;
        auto t = m_queue.front();
        m_queue.pop();
        m_set.erase(t);
        return true;
    }

    void insert(T const& _t) { push(_t); }
    size_t count(T const& _t) const { return exist(_t) ? 1 : 0; }
    bool exist(T const& _t) const { return m_set.count(_t) > 0; }
    size_t size() const { return m_set.size(); }

    void clear()
    {
        m_set.clear();
        while (!m_queue.empty())
            m_queue.pop();
    }

private:
    std::unordered_set<T> m_set;
    std::queue<T> m_queue;
};
}  // namespace dev
