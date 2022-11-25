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
 */

#pragma once

//#include <sys/time.h>
#include "Log.h"
#include "RefDataContainer.h"
#include <boost/algorithm/string.hpp>
#include <boost/container/options.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/multiprecision/cpp_int.hpp>
#include <boost/thread.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bcos
{
using namespace boost::multiprecision::literals;

// vector of byte data
using byte = uint8_t;
using bytes = std::vector<byte>;
using bytesPointer = std::shared_ptr<std::vector<byte>>;
using bytesConstPtr = std::shared_ptr<const bytes>;
using bytesRef = RefDataContainer<byte>;
using bytesConstRef = RefDataContainer<byte const>;

// Numeric types.
using bigint = boost::multiprecision::number<boost::multiprecision::cpp_int_backend<>>;

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

// Map types.
using BytesMap = std::map<bytes, bytes>;
// Fixed-length string types.
using string32 = std::array<char, 32>;
// Map types.
using HexMap = std::map<bytes, bytes>;

// Null/Invalid values for convenience.
extern bytes const NullBytes;
u256 constexpr Invalid256 =
    0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff_cppui256;


using Mutex = std::mutex;
using RecursiveMutex = std::recursive_mutex;
using SharedMutex = boost::shared_mutex;

using Guard = std::lock_guard<std::mutex>;
using UniqueGuard = std::unique_lock<std::mutex>;
using RecursiveGuard = std::lock_guard<std::recursive_mutex>;
using ReadGuard = boost::shared_lock<boost::shared_mutex>;
using UpgradableGuard = boost::upgrade_lock<boost::shared_mutex>;
using UpgradeGuard = boost::upgrade_to_unique_lock<boost::shared_mutex>;
using WriteGuard = boost::unique_lock<boost::shared_mutex>;

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

inline bool isalNumStr(std::string const& _stringData)
{
    for (auto ch : _stringData)
    {
        if (isalnum(ch))
        {
            continue;
        }
        return false;
    }
    return true;
}

enum class WithExisting : int
{
    Trust = 0,
    Verify,
    Rescue,
    Kill
};

/// Get the current time in seconds since the epoch in UTC(ms)
uint64_t utcTime();
uint64_t utcSteadyTime();

/// Get the current time in seconds since the epoch in UTC(us)
uint64_t utcTimeUs();
uint64_t utcSteadyTimeUs();

// get the current datatime
std::string getCurrentDateTime();

struct Exception;
// callback when throw exceptions
void errorExit(std::stringstream& _exitInfo, Exception const& exception);

/// Reference to a slice of buffer that also owns the buffer.
///
/// This is extension to the concept C++ STL library names as array_view
/// (also known as gsl::span, array_ref, here RefDataContainer) -- reference to
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
class owning_bytes_ref : public RefDataContainer<byte const>
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

template <typename T>
class HolderForDestructor
{
public:
    HolderForDestructor(std::shared_ptr<T> _elementsToDestroy)
      : m_elementsToDestroy(std::move(_elementsToDestroy))
    {}
    void operator()() {}

private:
    // Elements to be deconstructed
    std::shared_ptr<T> m_elementsToDestroy;
};

void pthread_setThreadName(std::string const& _n);

/*
template <class... Ts>
struct overloaded : Ts...
{
    using Ts::operator()...;
};
// explicit deduction guide (not needed as of C++20)
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;
*/

}  // namespace bcos
