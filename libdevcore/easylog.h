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

#pragma warning(push)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#ifdef FISCO_EASYLOG
#include "easylogging++.h"
#else
#include "Log.h"
#endif
#pragma warning(pop)
#pragma GCC diagnostic pop
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

// BCOS log format
#define LOG_BADGE(_NAME) "[" << (_NAME) << "]"
#define LOG_DESC(_DESCRIPTION) (_DESCRIPTION)
#define LOG_KV(_K, _V) "," << (_K) << "=" << (_V)
