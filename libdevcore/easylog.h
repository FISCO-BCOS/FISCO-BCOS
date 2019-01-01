/*
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
 * along with FISCO-BCOS.  If not, see <http://www.gnu.org/licenses/>
 * (c) 2016-2018 fisco-dev contributors.
 */
/**
 * @brief : override append function of easylogging++
 * @file: easylog.h
 * @author: yujiechen
 * @date: 2017
 */

#pragma once

#include "Common.h"
#include "CommonData.h"
#include "CommonIO.h"
#include "FixedHash.h"

#ifdef FISCO_EASYLOG
#include "easylogging++.h"
#else
#include "Log.h"
#endif

#include "vector_ref.h"
#include <chrono>
#include <ctime>
//#include "Terminal.h"
#include <map>
#include <string>
namespace dev
{
class ThreadContext
{
public:
    ThreadContext(std::string const& _info) { push(_info); }
    ~ThreadContext() { pop(); }

    static void push(std::string const& _n);
    static void pop();
    static std::string join(std::string const& _prior);
};

void pthread_setThreadName(std::string const& _n);

/// Set the current thread's log name.
std::string getThreadName();
}  // namespace dev

#ifdef FISCO_EASYLOG
#define MY_CUSTOM_LOGGER(LEVEL) CLOG(LEVEL, "default", "fileLogger")
#undef LOG
#define LOG(LEVEL) CLOG(LEVEL, "default", "fileLogger")
#undef VLOG
#define VLOG(LEVEL) CVLOG(LEVEL, "default", "fileLogger")
#define LOGCOMWARNING LOG(WARNING) << "common|"
#endif
namespace dev
{
class LogOutputStreamBase
{
public:
    LogOutputStreamBase() {}

    void append(unsigned long _t) { m_sstr << toString(_t); }
    void append(long _t) { m_sstr << toString(_t); }
    void append(unsigned int _t) { m_sstr << toString(_t); }
    void append(int _t) { m_sstr << toString(_t); }
    void append(bigint const& _t) { m_sstr << toString(_t); }
    void append(u256 const& _t) { m_sstr << toString(_t); }
    void append(u160 const& _t) { m_sstr << toString(_t); }
    void append(double _t) { m_sstr << toString(_t); }
    template <unsigned N>
    void append(FixedHash<N> const& _t)
    {
        m_sstr << _t.abridged();
    }
    void append(h160 const& _t) { m_sstr << _t.abridged(); }
    void append(h256 const& _t) { m_sstr << _t.abridged(); }
    void append(h512 const& _t) { m_sstr << _t.abridged(); }
    void append(std::string const& _t) { m_sstr << _t; }
    void append(bytes const& _t) { m_sstr << toHex(_t); }
    void append(bytesConstRef _t) { m_sstr << toHex(_t); }

    template <class T>
    void append(std::vector<T> const& _t)
    {
        m_sstr << "[";
        int n = 0;
        for (auto const& i : _t)
        {
            m_sstr << (n++ ? ", " : "");
            append(i);
        }
        m_sstr << "]";
    }
    template <class T>
    void append(std::set<T> const& _t)
    {
        m_sstr << "{";
        int n = 0;
        for (auto const& i : _t)
        {
            m_sstr << (n++ ? ", " : "");
            append(i);
        }
        m_sstr << "}";
    }
    template <class T, class U>
    void append(std::map<T, U> const& _t)
    {
        m_sstr << "{";
        int n = 0;
        for (auto const& i : _t)
        {
            m_sstr << (n++ ? ", " : "");
            append(i.first);
            m_sstr << (n++ ? ": " : "");
            append(i.second);
        }
        m_sstr << "}";
    }
    template <class T>
    void append(std::unordered_set<T> const& _t)
    {
        m_sstr << "{";
        int n = 0;
        for (auto const& i : _t)
        {
            m_sstr << (n++ ? ", " : "");
            append(i);
        }
        m_sstr << "}";
    }
    template <class T, class U>
    void append(std::unordered_map<T, U> const& _t)
    {
        m_sstr << "{";
        int n = 0;
        for (auto const& i : _t)
        {
            m_sstr << (n++ ? ", " : "");
            append(i.first);
            m_sstr << (n++ ? ": " : "");
            append(i.second);
        }
        m_sstr << "}";
    }
    template <class T, class U>
    void append(std::pair<T, U> const& _t)
    {
        m_sstr << "(";
        append(_t.first);
        m_sstr << ", ";
        append(_t.second);
        m_sstr << ")";
    }
    template <class T>
    void append(T const& _t)
    {
        m_sstr << toString(_t);
    }

    template <class T>
    LogOutputStreamBase& operator<<(T const& _t)
    {
        append(_t);
        return *this;
    }

protected:
    std::stringstream m_sstr;  ///< The accrued log entry.
};

}  // namespace dev
