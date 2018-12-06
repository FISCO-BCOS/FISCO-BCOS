/*
	This file is part of cpp-ethereum.

	cpp-ethereum is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	cpp-ethereum is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with cpp-ethereum.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
 * @file: StateMonitor.h
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#pragma once

#include <iostream>    
#include <boost/asio.hpp>  
#include <boost/thread.hpp>  
#include <boost/date_time/posix_time/posix_time.hpp>  
#include <atomic>
#include <sys/time.h>
#include <float.h>
#include <time.h>
#include <cstdio>
#include <algorithm>
#include <climits>
#include <map>
#include <unordered_map>
#include "easylog.h"

namespace statemonitor
{ 
using data_t = double;

class StateContainer
{
    using msec_t = double;
    using sec_t = double;
    #define MAX_TIME DBL_MAX
    #define NO_INIT 0
public:
    uint64_t record_cnt, success_cnt;   
    data_t max_data, min_data, 
           data_sum; //当前次export的time_cost总和

    StateContainer()
    {
        reset();
    }

    virtual ~StateContainer(){};

    void recordStart(int child_code);
    void recordEnd(int is_success, int child_code);
    void recordOnce(int is_success, data_t value);
    bool exportState(std::string &state);
    void reset();

private:
    msec_t _last_export_time;
    std::map<int, msec_t> _mp_record_start_time;

    msec_t getExportInterval();
    msec_t getRecordInterval(int child_code);
    msec_t getCurrentTime();
    void setRecordStartTime(int child_code);

    void resetState();
    
    inline data_t convert(unsigned int x){return data_t(x);}
    inline data_t convert(unsigned long long x){return data_t(x);}
    inline data_t convert(float x){return data_t(x);}
    inline data_t convert(double x){return data_t(x);}
};

class StateReporter
{
public:
    static void report(std::string state_str)
    {
        NormalStatLog()  << "##State Report: " << state_str;
    }
};

class StateMonitor
{
public:
    StateContainer state;

    StateMonitor():state(){};
    virtual ~StateMonitor(){};

    void report(int code, std::string& name, std::string& info);
};

//以时间为周期，上报state
class StateMonitorByTime : public StateMonitor
{
public:
    using sec_t = uint64_t;
    int code;
    std::string state_name, state_info;

    StateMonitorByTime(uint64_t interval):_interval(interval),_is_timer_on(false){}
    StateMonitorByTime():_interval(10),_is_timer_on(false){} //默认10s上报一次
    virtual ~StateMonitorByTime(){};

    void recordStart(int code, uint64_t interval, int child_code);
    void recordEnd(int code, std::string& name, std::string& info, int is_success, int child_code);
    void recordOnce(int code, uint64_t interval, data_t value, std::string& name, std::string& info, int is_success);
    void timerToReport(sec_t sec_time);
private:
    sec_t _interval; 
    bool _is_timer_on;

    //按周期（_interval）进行上报
    void startTimer(sec_t interval);
};

//以次数为周期，上报state
class StateMonitorByNum : public StateMonitor
{
    using cnt_t = uint64_t;    
public:
    StateMonitorByNum(uint64_t report_per_num)
            : _report_per_num(report_per_num),
              _cnt(0){};

    StateMonitorByNum()
            : _report_per_num(0),
            _cnt(0){};

    virtual ~StateMonitorByNum(){};

    void recordStart(int code, uint64_t report_per_num, int child_code);
    void recordEnd(int code, std::string& name, std::string& info, int is_success, int child_code);
    void recordOnce(int code, uint64_t report_per_num, data_t value, std::string& name, std::string& info, int is_success);
private:
    cnt_t _report_per_num;
    cnt_t _cnt;
};


inline int getChildCodeFromThreadId()
{
#ifdef __APPLE__
    uint64_t tid;
    pthread_threadid_np(NULL, &tid);
    return tid & 0xfffff;
#else
    return pthread_self() & 0xfffff;
#endif
}

void enableTimerLoop();

void recordStateByTimeStart(int code, uint64_t interval,
                            int child_code = getChildCodeFromThreadId());

void recordStateByTimeEnd(int code, std::string &&name, std::string &&info,
                          int is_success = 1, //  > 0:  成功(success)，记录此end时间点，更新success计数器。 
                                              // == 0： 失败(failed)，记录此end时间点，不更新success计数器。
                                              //  < 0:  不可用(not avaliable)，不记录此end时间点，同时删除相应child_code的start记录的时间点。
                          int child_code = getChildCodeFromThreadId());

void recordStateByTimeOnce(int code, uint64_t interval,
                            data_t value, std::string &&name, std::string &&info,
                            int is_success = 1);  //  > 0:  成功(success)，记录此value，更新success计数器。 
                                                  // == 0： 失败(failed)，记录此value，不更新success计数器。
                                                  //  < 0:  不可用(not avaliable)，不记录此value，不进行任何操作

void recordStateByNumStart(int code, uint64_t report_per_num,
                           int child_code = getChildCodeFromThreadId());

void recordStateByNumEnd(int code, std::string &&name, std::string &&info,
                         int is_success = 1, //含义与recordStateByTimeEnd相同
                         int child_code = getChildCodeFromThreadId());

void recordStateByNumOnce(int code, uint64_t report_per_num,
                          data_t value, std::string &&name, std::string &&info,
                          int is_success = 1);  //含义与recordStateByTimeOnce相同

extern bool shouldExit;
extern bool isRunning; 
}