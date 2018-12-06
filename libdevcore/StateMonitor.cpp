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
 * @file: StateMonitor.cpp
 * @author: fisco-dev
 * 
 * @date: 2017
 */

#include <iostream>
#include <sstream>
#include <boost/asio.hpp>  
#include <boost/thread/thread.hpp>  
#include <boost/thread/mutex.hpp>
#include <boost/date_time/posix_time/posix_time.hpp> 
#include <atomic>
#include <sys/time.h>
#include <time.h>
#include <algorithm>
#include <climits>

#include "StateMonitor.h"

using namespace std;    
using namespace boost; 


namespace statemonitor
{

bool shouldExit = false;
bool isRunning = false; 

static std::map<int, StateMonitorByTime> monitorByTimeTable;
static std::map<int, StateMonitorByNum> monitorByNumTable;

static std::map<int, boost::mutex> monitorByTimeLockTable, monitorByNumLockTable;
static boost::mutex monitorByTimeTableLock, monitorByNumTableLock; 

#define LOCK_MONITOR(_Type)                                      \
    monitorBy##_Type##TableLock.lock();                          \
    boost::mutex &monitor_lock =  monitorBy##_Type##LockTable[code];    \
    monitorBy##_Type##TableLock.unlock();                        \
    monitor_lock.lock();


#define UNLOCK_MONITOR() \
    monitor_lock.unlock();


/*
*   StateContainer
*/
void StateContainer::recordStart(int child_code)
{
    //_record_start_time = getCurrentTime();
    setRecordStartTime(child_code);

    //如果是第一次进来_last_export_time未初始化，则为其赋当前时间
    if (NO_INIT == _last_export_time) 
        _last_export_time = getCurrentTime();
}

void StateContainer::recordEnd(int is_success, int child_code)
{
    if (NO_INIT == _last_export_time)
        return; //如果未初始化，直接退出

    if (is_success < 0)
    {
        //此条记录不可用，删除此child_code对应的start时记录的时间点，并退出
        auto it = _mp_record_start_time.find(child_code);
        if (it != _mp_record_start_time.end())
            _mp_record_start_time.erase(it);
        return;
    }

    msec_t time_cost = getRecordInterval(child_code);
    if (time_cost < 0)
        return;

    data_t value = convert(time_cost);
    max_data = max(max_data, value);
    min_data = min(min_data, value);

    data_sum += value;
    ++ record_cnt;

    if(is_success > 0)
        ++ success_cnt;
}

void StateContainer::recordOnce(int is_success, data_t value)
{
    //recordOnce相当于同时进行了一组start end，用户自定义传入差值 value

    //如果是第一次进来_last_export_time未初始化，则为其赋当前时间
    if (NO_INIT == _last_export_time) 
        _last_export_time = getCurrentTime();
    
    if (is_success < 0)
        return;   //数据不可用，直接退出
    
    max_data = max(max_data, value);
    min_data = min(min_data, value);

    data_sum += value;
    ++ record_cnt;

    if(is_success > 0)
        ++ success_cnt;
}

#define stateToString(state) (string(#state) + string(":") + to_string(state) + string(" "))
bool StateContainer::exportState(string &state)
{
    /*
        *  导出内容                 单位
        *  start_timestamp          ms
        *  end_timestamp            ms
        *  input_rate               qps
        *  avg_rate                 qps
        *  
        *  max_data            
        *  min_data            
        *  avg_data            
        * 
        *  export_interval_msec     ms
        *  record_cnt               次
        *  success_cnt              次
        *  success_percent           %
    */
    uint64_t start_timestamp = ceil(_last_export_time);
    msec_t export_interval_msec = getExportInterval();
    uint64_t end_timestamp = start_timestamp + ceil(export_interval_msec);
    double export_interval_sec = double(export_interval_msec) / 1000;  

    if (0 == record_cnt)
        return false; //如果记录数为0，一次数据都没有，导出失败

    data_t avg_data;
    double input_rate, max_rate, min_rate, success_percent;

    avg_data = data_sum / record_cnt; 
    input_rate = double(record_cnt) / export_interval_sec;
    success_percent = 100 * double(success_cnt) / double(record_cnt);

    std::stringstream ss;
    ss << " " 
        << "start:" << start_timestamp << " | "
        << "end:" << end_timestamp << " | "
        << "in_Rto:" << input_rate << " | "
        << "max:" << max_data << " | "
        << "min:" << min_data << " | "
        << "avg:" << avg_data << " | "
        << "interval:" << export_interval_msec << " | "
        << "cnt:" << record_cnt << " | "
        << "suc_cnt:" << success_cnt << " | "
        << "suc_per:" << success_percent;
    state = ss.str();
    // state = string();
    // state += stateToString(start_timestamp);
    // state += stateToString(end_timestamp);
    // state += stateToString(input_rate);
    // state += stateToString(max_data);
    // state += stateToString(min_data);
    // state += stateToString(avg_data);
    // state += stateToString(export_interval_msec);
    // state += stateToString(record_cnt);
    // state += stateToString(success_cnt);
    // state += stateToString(success_percent);

    resetState();

    return true;
}

void StateContainer::reset()
{
    resetState();
    _last_export_time = NO_INIT;
    _mp_record_start_time = std::map<int, msec_t>(); //赋空值，释放内存
}

StateContainer::msec_t StateContainer::getExportInterval()
{
    msec_t current = getCurrentTime();
    msec_t interval = current - _last_export_time;
    _last_export_time = current;

    return interval;
}

StateContainer::msec_t StateContainer::getRecordInterval(int child_code)
{
    // msec_t record_start_time = _mp_record_start_time.at(child_code);
    auto it = _mp_record_start_time.find(child_code); //recordStart和recordEnd必须在保证在同一个child_code才能被记录下来
    if (it != _mp_record_start_time.end()){
        msec_t record_start_time = it->second;
        _mp_record_start_time.erase(it); //此child_code的记录完成使命，删掉节省空间  
        return getCurrentTime() - record_start_time;
    } else {
        // todo 报警一下，可能出现了函数嵌套多次调用
        return -1;
    } 
}

StateContainer::msec_t StateContainer::getCurrentTime()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t sec = tv.tv_sec;
    return msec_t(sec) * 1000 + msec_t(tv.tv_usec) / 1000;
}

void StateContainer::setRecordStartTime(int child_code)
{
    _mp_record_start_time[child_code] = getCurrentTime();
}

void StateContainer::resetState()
{
    record_cnt = 0;
    success_cnt = 0;
    max_data = 0;
    min_data = MAX_TIME;
    data_sum = 0;
}

/*
*   StateMonitor
*/
void StateMonitor::report(int code, string& name, string& info)
{
    string state_str;

    if (!state.exportState(state_str))
    {
        //cout << "Haven't collected any data, report failed" << endl;
        return; //导出失败，直接返回，不发送错误数据
    }

    //report的格式：[code][name][info] state_str    
    stringstream ss;
    ss << "[" << code << "][" << name << "][" << info << "] " << state_str;

    StateReporter::report(ss.str());
}

/*
*   StateMonitorByTime
*/
void StateMonitorByTime::recordStart(int code, uint64_t interval, int child_code)
{
    if (interval != _interval || !_is_timer_on)
    {
        NormalStatLog() << "[" << code << "]StateMonitorByTime::interval change to " << interval;
        startTimer(interval);
    }
    state.recordStart(child_code);
}

void StateMonitorByTime::recordEnd(int code, string& name, string& info, int is_success, int child_code)
{
    if (is_success >= 0) //只要不是不可用的，都会被记录
    {
        this->code = code;
        state_name = name;
        state_info = info;
    }
    state.recordEnd(is_success, child_code);
}

void StateMonitorByTime::recordOnce(int code, uint64_t interval, data_t value, std::string& name, std::string& info, int is_success)
{
    if (interval != _interval || !_is_timer_on)
    {
        NormalStatLog() << "[" << code << "]StateMonitorByTime::interval change to " << interval;
        startTimer(interval);
    }

    if (is_success >= 0) //只要不是不可用的，都会被记录
    {
        this->code = code;
        state_name = name;
        state_info = info;
    }

    state.recordOnce(is_success, value);
}

//按周期（_interval）进行上报
void StateMonitorByTime::timerToReport(sec_t sec_time) 
{
    //std::cout << "sec_time: " << sec_time << " interval: " << _interval << " code: " << code << " state_name: " << state_name << std::endl;
    if (_is_timer_on && _interval != 0 && sec_time % _interval == 0)
    {
        //sleep(_interval); //若在sleep时修改了interval，也要等本次跑完，下次新的interval才生效
        LOCK_MONITOR(Time); //此处是在线程中跑，共享name和info变量，所以需要加与入口一样的锁
        report(code, state_name, state_info);
        UNLOCK_MONITOR();
    }
}

void StateMonitorByTime::startTimer(sec_t interval)
{
    _interval = interval;

    if (_is_timer_on)
        return; //避免重复开timer线程

    _is_timer_on = true;

    enableTimerLoop();
}

void globalTimerLoop()
{
    StateMonitorByTime::sec_t sec_time = 0, gap = 1;
    while(!shouldExit)
    {
        sec_time += gap; //溢出没关系
        sleep(gap);
        for (auto &state : monitorByTimeTable)
        {
            try
            {
                if(shouldExit) break;
                //std::cout << "Report: " << state.first << std::endl;
                state.second.timerToReport(sec_time);
            }
            catch (std::exception &e)
            {
                LOG(ERROR) << "Report failed for: " << e.what();
            }
        }
    }
    isRunning = false;
}

void enableTimerLoop()
{
    static std::mutex enable_lock;
    enable_lock.lock();
    if (!isRunning)
    {
        isRunning = true;
        enable_lock.unlock();
        boost::thread timer_thread(&globalTimerLoop);
    }
    else
        enable_lock.unlock();
}

/*
*   StateMonitorByNum
*/
void StateMonitorByNum::recordStart(int code, uint64_t report_per_num, int child_code)
{
    if (_report_per_num != report_per_num)
    {
        NormalStatLog() << "[" << code << "]StateMonitorByNum::report_per_num change to " << report_per_num;
        _report_per_num = report_per_num;

        state.reset();
        _cnt = 0;
    }
    state.recordStart(child_code);
}

void StateMonitorByNum::recordEnd(int code, string &name, string &info, int is_success, int child_code)
{
    state.recordEnd(is_success, child_code);

    if (is_success < 0)
        return; //表示此记录不可用，不被记录

    if (++_cnt < _report_per_num)
        return;

    //如果达到了次数，就要report
    report(code, name, info);
    _cnt = 0;
}

void StateMonitorByNum::recordOnce(int code, uint64_t report_per_num, data_t value, std::string& name, std::string& info, int is_success)
{
    if (_report_per_num != report_per_num)
    {
        NormalStatLog() << "[" << code << "]StateMonitorByNum::report_per_num change to " << report_per_num;
        _report_per_num = report_per_num;

        state.reset();
        _cnt = 0;
    }

    state.recordOnce(is_success, value);

    if (is_success < 0)
        return; //表示此记录不可用，不被记录

    if (++_cnt < _report_per_num)
        return;

    //如果达到了次数，就要report
    report(code, name, info);
    _cnt = 0;
}

void recordStateByTimeStart(int code, uint64_t interval, int child_code)
{
    LOCK_MONITOR(Time)

    monitorByTimeTable[code].recordStart(code, interval, child_code); //code找不到会自己创建

    UNLOCK_MONITOR()
}

void recordStateByTimeEnd(int code, string &&name, string &&info, int is_success, int child_code)
{
    LOCK_MONITOR(Time)

    // monitorByTimeTable[code].recordEnd(code, name, info, is_success, child_code);
    auto it = monitorByTimeTable.find(code);
    if (it != monitorByTimeTable.end()) 
        it->second.recordEnd(code, name, info, is_success, child_code);

    UNLOCK_MONITOR()
}

void recordStateByTimeOnce(int code, uint64_t interval,
                           data_t value, std::string &&name, std::string &&info,
                           int is_success)
{
    LOCK_MONITOR(Time)
    
    monitorByTimeTable[code].recordOnce(code, interval, value, name, info, is_success);
    
    UNLOCK_MONITOR()    
}

void recordStateByNumStart(int code, uint64_t report_per_num, int child_code)
{
    LOCK_MONITOR(Num)

    monitorByNumTable[code].recordStart(code, report_per_num, child_code); //code找不到会自己创建

    UNLOCK_MONITOR()
}

void recordStateByNumEnd(int code, string &&name, string &&info, int is_success, int child_code)
{
    LOCK_MONITOR(Num)
    auto it = monitorByNumTable.find(code);
    if (it != monitorByNumTable.end()) 
        it->second.recordEnd(code, name, info, is_success, child_code);

    UNLOCK_MONITOR()
}

void recordStateByNumOnce(int code, uint64_t report_per_num,
                          data_t value, std::string &&name, std::string &&info,
                          int is_success)
{
    LOCK_MONITOR(Num)

    monitorByNumTable[code].recordOnce(code, report_per_num, value, name, info, is_success);
    
    UNLOCK_MONITOR()    
}

}



