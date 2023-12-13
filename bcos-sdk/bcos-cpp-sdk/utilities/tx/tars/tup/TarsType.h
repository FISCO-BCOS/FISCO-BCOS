/**
 * Tencent is pleased to support the open source community by making Tars available.
 *
 * Copyright (C) 2016THL A29 Limited, a Tencent company. All rights reserved.
 *
 * Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
 * in compliance with the License. You may obtain a copy of the License at
 *
 * https://opensource.org/licenses/BSD-3-Clause
 *
 * Unless required by applicable law or agreed to in writing, software distributed
 * under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations under the License.
 */

#ifndef __TARS_TYPE_H__
#define __TARS_TYPE_H__

#ifdef __linux__
#include <netinet/in.h>
#elif _WIN32
#include <WinSock2.h>
#endif

#pragma GCC diagnostic ignored "-Wunused-parameter"

// #include <netinet/in.h>
#include <bcos-cpp-sdk/utilities/tx/tars/util/tc_platform.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <cassert>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>


namespace tars
{
/////////////////////////////////////////////////////////////////////////////////////
typedef bool Bool;
typedef char Char;
typedef short Short;
typedef float Float;
typedef double Double;
typedef int Int32;
struct TarsStructBase;

typedef unsigned char UInt8;
typedef unsigned short UInt16;
typedef unsigned int UInt32;

#if __WORDSIZE == 64
typedef long Int64;
#else
typedef long long Int64;
#endif

#ifndef TARS_MAX_STRING_LENGTH
#define TARS_MAX_STRING_LENGTH (100 * 1024 * 1024)
#endif
/*
#define tars__bswap_constant_32(x) \
    ((((x) & 0xff000000) >> 24) | (((x) & 0x00ff0000) >>  8) |           \
    (((x) & 0x0000ff00) <<  8) | (((x) & 0x000000ff) << 24))

#define tars__bswap_constant_64(x) \
    ((((x) & 0xff00000000000000ull) >> 56)                   \
    | (((x) & 0x00ff000000000000ull) >> 40)                     \
    | (((x) & 0x0000ff0000000000ull) >> 24)                     \
    | (((x) & 0x000000ff00000000ull) >> 8)                      \
    | (((x) & 0x00000000ff000000ull) << 8)                      \
    | (((x) & 0x0000000000ff0000ull) << 24)                     \
    | (((x) & 0x000000000000ff00ull) << 40)                     \
    | (((x) & 0x00000000000000ffull) << 56))
*/
#if (defined(__APPLE__) || defined(_WIN32))
#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN 1234
#endif
#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN 4321
#endif
#ifndef __BYTE_ORDER
#define __BYTE_ORDER __LITTLE_ENDIAN
#endif
#endif

#if __BYTE_ORDER == __BIG_ENDIAN
#define tars_ntohll(x) (x)
#define tars_htonll(x) (x)
#define tars_ntohf(x) (x)
#define tars_htonf(x) (x)
#define tars_ntohd(x) (x)
#define tars_htond(x) (x)
#else
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define tars_ntohll(x) tars_htonll(x)
//#     define tars_htonll(x)    tars__bswap_constant_64(x)
namespace detail
{
union bswap_helper
{
    Int64 i64;
    Int32 i32[2];
};
}  // namespace detail
inline Int64 tars_htonll(Int64 x)
{
    detail::bswap_helper h;
    h.i64 = x;
    Int32 tmp = htonl(h.i32[1]);
    h.i32[1] = htonl(h.i32[0]);
    h.i32[0] = tmp;
    return h.i64;
}
inline Float tars_ntohf(Float x)
{
    union
    {
        Float f;
        Int32 i32;
    } helper;

    helper.f = x;
    helper.i32 = htonl(helper.i32);

    return helper.f;
}
#define tars_htonf(x) tars_ntohf(x)
inline Double tars_ntohd(Double x)
{
    union
    {
        Double d;
        Int64 i64;
    } helper;

    helper.d = x;
    helper.i64 = tars_htonll(helper.i64);

    return helper.d;
}
#define tars_htond(x) tars_ntohd(x)
#endif
#endif

// type2name
template <typename T>
struct TarsClass
{
    static std::string name() { return T::className(); }
};
template <>
struct TarsClass<tars::Bool>
{
    static std::string name() { return "bool"; }
};
template <>
struct TarsClass<tars::Char>
{
    static std::string name() { return "char"; }
};
template <>
struct TarsClass<tars::Short>
{
    static std::string name() { return "short"; }
};
template <>
struct TarsClass<tars::Float>
{
    static std::string name() { return "float"; }
};
template <>
struct TarsClass<tars::Double>
{
    static std::string name() { return "double"; }
};
template <>
struct TarsClass<tars::Int32>
{
    static std::string name() { return "int32"; }
};
template <>
struct TarsClass<tars::Int64>
{
    static std::string name() { return "int64"; }
};
template <>
struct TarsClass<tars::UInt8>
{
    static std::string name() { return "short"; }
};
template <>
struct TarsClass<tars::UInt16>
{
    static std::string name() { return "int32"; }
};
template <>
struct TarsClass<tars::UInt32>
{
    static std::string name() { return "int64"; }
};
template <>
struct TarsClass<std::string>
{
    static std::string name() { return "string"; }
};
template <typename T>
struct TarsClass<std::vector<T> >
{
    static std::string name() { return std::string("list<") + TarsClass<T>::name() + ">"; }
};
template <typename T, typename U>
struct TarsClass<std::map<T, U> >
{
    static std::string name()
    {
        return std::string("map<") + TarsClass<T>::name() + "," + TarsClass<U>::name() + ">";
    }
};

namespace detail
{
// is_convertible
template <int N>
struct type_of_size
{
    char elements[N];
};

typedef type_of_size<1> yes_type;
typedef type_of_size<2> no_type;

namespace meta_detail
{
struct any_conversion
{
    template <typename T>
    any_conversion(const volatile T&);
    template <typename T>
    any_conversion(T&);
};

template <typename T>
struct conversion_checker
{
    static no_type _m_check(any_conversion...);
    static yes_type _m_check(T, int);
};
}  // namespace meta_detail

template <typename From, typename To>
class is_convertible
{
    static From _m_from;

public:
    enum
    {
        value =
            sizeof(meta_detail::conversion_checker<To>::_m_check(_m_from, 0)) == sizeof(yes_type)
    };
};

template <typename T>
struct type2type
{
    typedef T type;
};

template <typename T, typename U>
struct is_same_type
{
    enum
    {
        value = is_convertible<type2type<T>, type2type<U> >::value
    };
};

// enable_if
template <bool B, class T = void>
struct enable_if_c
{
    typedef T type;
};

template <class T>
struct enable_if_c<false, T>
{
};

template <class Cond, class T = void>
struct enable_if : public enable_if_c<Cond::value, T>
{
};

template <bool B, class T = void>
struct disable_if_c
{
    typedef T type;
};

template <class T>
struct disable_if_c<true, T>
{
};

template <class Cond, class T = void>
struct disable_if : public disable_if_c<Cond::value, T>
{
};
}  // namespace detail

////////////////////////////////////////////////////////////////////////////////////////////////////

}  // namespace tars
#endif
