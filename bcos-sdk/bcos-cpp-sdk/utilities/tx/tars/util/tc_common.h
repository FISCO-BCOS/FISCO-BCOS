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

#ifndef __TC_COMMON_H
#define __TC_COMMON_H

#include <bcos-cpp-sdk/utilities/tx/tars/util/tc_platform.h>

#include <time.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <cassert>
#include <list>
#include <thread>
#include <cstdio>
#include <string>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <sstream>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <stack>
#include <vector>
#include <list>
#include <thread>
#include <memory>

#if TARGET_PLATFORM_WINDOWS

#ifndef ssize_t
#define ssize_t __int64
#endif

#endif

namespace tars
{
/////////////////////////////////////////////////
/**
* @file tc_common.h
* @brief  帮助类,都是静态函数,提供一些常用的函数 .
* @brief  Helper Class. There're all static functions in this which provides some commonly used functions
*
*/
/////////////////////////////////////////////////

/**
* @brief  基础工具类，提供了一些非常基本的函数使用.
* @brief  Basic Utility Class. Some basic functions are provided.
*
* 这些函数都是以静态函数提供。 包括以下几种函数:
* These functions are provided as static functions.It includes the following functions:
*
* Trim类函数,大小写转换函数,分隔字符串函数（直接分隔字符串，
* Trim class functions, case conversion functions, delimited string functions (directly delimited strings, numbers, etc.), 
* 
* 数字等）,时间相关函数,字符串转换函数,二进制字符串互转函数,
* time-dependent functions, string conversion functions, binary string conversion functions, 
* 
* 替换字符串函数,Ip匹配函数,判断一个数是否是素数等
* replacement string functions, IP matching functions, determining whether a number is a prime number, etc.
*/
class UTIL_DLL_API TC_Common
{
public:

    static const float  _EPSILON_FLOAT;
    static const double _EPSILON_DOUBLE;
    static const int64_t ONE_DAY_MS = 24 * 3600 * 1000L;
    static const int64_t ONE_HOUR_MS = 1 * 3600 * 1000L;
    static const int64_t ONE_MIN_MS = 60 * 1000L;
    static const int64_t ONE_DAY_SEC = 86400;

    /**
    * @brief  跨平台sleep
    * @brief  Cross Platform Sleep
    */
    static void sleep(uint32_t sec);
    static void msleep(uint32_t ms);

    /**
    * @brief  浮点数比较,double 默认取6位精度，float默认6位精度
    * @brief  Floating Number Comparison, double defaults to be 6-bit precision, and float defaults to be 6-bit precision as well.
    */
    static bool equal(double x, double y, double epsilon = _EPSILON_DOUBLE);
    static bool equal(double x, double y, float epsilon );

    static bool equal(float x, float y, float epsilon = _EPSILON_FLOAT);
    static bool equal(float x, float y, double epsilon );

    /**
    * @brief  vector double 各种场景比较函数
    * @brief  vector double, comparison functions for various scenarios
    */
    static bool equal(const std::vector<double> & vx, const std::vector<double>& vy, double epsilon = _EPSILON_DOUBLE);
    static bool equal(const std::vector<double>& vx, const std::vector<double>& vy, float epsilon );
    static bool equal(const std::vector<float>& vx, const std::vector<float> & vy, float epsilon = _EPSILON_FLOAT);
    static bool equal(const std::vector<float>& vx, const std::vector<float>& vy, double epsilon );

	static bool equal(const std::set<double> & vx, const std::set<double>& vy, double epsilon = _EPSILON_DOUBLE);
	static bool equal(const std::set<double>& vx, const std::set<double>& vy, float epsilon );
	static bool equal(const std::set<float>& vx, const std::set<float> & vy, float epsilon = _EPSILON_FLOAT);
	static bool equal(const std::set<float>& vx, const std::set<float>& vy, double epsilon );

	static bool equal(const std::unordered_set<double> & vx, const std::unordered_set<double>& vy, double epsilon = _EPSILON_DOUBLE);
	static bool equal(const std::unordered_set<double>& vx, const std::unordered_set<double>& vy, float epsilon );
	static bool equal(const std::unordered_set<float>& vx, const std::unordered_set<float> & vy, float epsilon = _EPSILON_FLOAT);
	static bool equal(const std::unordered_set<float>& vx, const std::unordered_set<float>& vy, double epsilon );

    /**
    * @brief  map中如果key或者value为double/float字段，则用此模板函数比较
    * @brief  In map, if the key or value is the double/float field, use this template function to compare.
    */
    template<typename V, typename E>
    static bool equal(const V& x, const V& y, E eps);
    template<typename K, typename V, typename D, typename A , typename E=double>
    static bool equal(const std::map<K, V, D, A>& mx , const std::map<K, V, D, A>& my, E epsilon = _EPSILON_DOUBLE);
	template<typename K, typename V, typename D, typename A , typename E=double>
	static bool equal(const std::unordered_map<K, V, D, A>& mx , const std::unordered_map<K, V, D, A>& my, E epsilon = _EPSILON_DOUBLE);

    /**
     * 固定宽度填充字符串, 用于输出对齐格式用(默认右填充)
     * Fixed width filled string for output alignment format (default right-padding)
     * @param s
     * @param c
     * @param n
     * @return
     */
    static std::string outfill(const std::string& s, char pad = ' ', size_t n = 50, bool rightPad=true)
    {
        if(n <= s.length())
            return s;

        if(rightPad)
            return (s + std::string((n - s.length()), pad));

        return (std::string((n - s.length()), pad) + s);
    }

    /**
    * @brief  去掉头部以及尾部的字符或字符串.
    * @brief  Remove the head and the tail characters or strings
    *
    * @param sStr    输入字符串
    * @param sStr    input string
    * @param s       需要去掉的字符
    * @param s       the characters which need to be removed
    * @param bChar   如果为true, 则去掉s中每个字符; 如果为false, 则去掉s字符串
    * @param bChar   bool : true, Remove each character in 's'; false, remove the s String
    * @return string 返回去掉后的字符串
    * @return string Return the removed string
    */
    static std::string trim(const std::string &sStr, const std::string &s = " \r\n\t", bool bChar = true);

    /**
    * @brief  去掉左边的字符或字符串.
    * @brief  Remove left character or string
    *
    * @param sStr    输入字符串
    * @param sStr    input string 
    * @param s       需要去掉的字符
    * @param s       the characters which need to be removed
    * @param bChar   如果为true, 则去掉s中每个字符; 如果为false, 则去掉s字符串
    * @param bChar   bool : true, Remove each character in 's'; false, remove the s String
    * @return string 返回去掉后的字符串
    * @return string Return the removed string
    */
    static std::string trimleft(const std::string &sStr, const std::string &s = " \r\n\t", bool bChar = true);

    /**
    * @brief  去掉右边的字符或字符串.
    * @brief  Remove right character or string
    *
    * @param sStr    输入字符串
    * @param sStr    input string 
    * @param s       需要去掉的字符
    * @param s       the characters which need to be removed
    * @param bChar   如果为true, 则去掉s中每个字符; 如果为false, 则去掉s字符串
    * @param bChar   bool : true, Remove each character in 's'; false, remove the s String
    * @return string 返回去掉后的字符串
    * @return string Return the removed string
    */
    static std::string trimright(const std::string &sStr, const std::string &s = " \r\n\t", bool bChar = true);

    /**
    * @brief  字符串转换成小写.
    * @brief  Convert string to lowercase.
    *
    * @param sString  字符串
    * @param sString  String
    * @return string  转换后的字符串
    * @return string  the converted string
    */
    static std::string lower(const std::string &sString);

    /**
    * @brief  字符串转换成大写.
    * @brief  Convert string to uppercase.
    *
    * @param sString  字符串
    * @param sString  string
    * @return string  转换后的大写的字符串
    * @return string  the converted string
    */
    static std::string upper(const std::string &sString);

    /**
    * @brief  字符串是否都是数字的.
    * @brief  Whether strings are all numbers or not.
    *
    * @param sString  字符串
    * @param sString  std::string
    * @return bool    是否是数字
    * @return bool    whether number or not
    */
    static bool isdigit(const std::string &sInput);

    /**
    * @brief  字符串转换成时间结构.
    * @brief  Convert std::string to time structure.
    *
    * @param sString  字符串时间格式
    * @param sString  std::string Time Format 
    * @param sFormat  格式
    * @param sFormat  format
    * @param stTm     时间结构
    * @param stTm     time structure
    * @return         0: 成功, -1:失败
    * @return         0: success, -1: fail
    */
    static int str2tm(const std::string &sString, const std::string &sFormat, struct tm &stTm);

    /**
     * @brief GMT格式的时间转化为时间结构
     * @brief Conversion of time into time structure in GMT format
     *
     * eg.Sat, 06 Feb 2010 09:29:29 GMT, %a, %d %b %Y %H:%M:%S GMT
     *
     * 可以用mktime换成time_t, 但是注意时区 可以用mktime(&stTm)
     * You can replace time_t with mktime. Be careful about the time zones, and it can be used with mktime (&stTm)
     *
     * - timezone换成本地的秒(time(NULL)值相同) timezone是系统的 ,
     * - timezone changes costs to local seconds (time (NULL) values are the same). Timezones is systematic.
     *
     * 需要extern long timezone;
     * need extern long timezone;
     *
     * @param sString  GMT格式的时间，本地时间
     * @param sString  time in GMT format
     * @param stTm     转换后的时间结构
     * @param stTm     converted Time Structure
     * @return         0: 成功, -1:失败
     * @return         0: success, -1: fail
     */
    static int strgmt2tm(const std::string &sString, struct tm &stTm);

    /**
    * @brief  格式化的字符串时间转为时间戳.
    * @brief  Format time std::string to timestamp
    *
    * @param sString  格式化的字符串时间，本地时间
    * @param sString  format time std::string
    * @param sFormat  格式化的字符串时间的格式，默认为紧凑格式
    * @param sFormat  format of formatted std::string time
    * @return time_t  转换后的时间戳
    * @return time_t  the converted timestamp
    */
    static time_t str2time(const std::string &sString, const std::string &sFormat = "%Y%m%d%H%M%S");

    /**
    * @brief  时间转换成字符串.
    * @brief  Convert time into std::string.
    *
    * @param stTm     时间结构
    * @param stTm     time structure
    * @param sFormat  需要转换的目标格式，默认为紧凑格式
    * @param sFormat  Target format to be converted, default to compact format
    * @return string  转换后的时间字符串
    * @return string  converted time std::string
    */
    static std::string tm2str(const struct tm &stTm, const std::string &sFormat = "%Y%m%d%H%M%S");

    /**
    * @brief  时间转换成字符串.
    * @brief  Convert time into std::string
    *
    * @param t        时间结构
    * @param t        time structure
    * @param sFormat  需要转换的目标格式，默认为紧凑格式
    * @param sFormat  Target format to be converted, default to compact format
    * @return string  转换后的时间字符串
    * @return string  converted time std::string
    */
    static std::string tm2str(const time_t &t, const std::string &sFormat = "%Y%m%d%H%M%S");

    /**
    * @brief  时间转换tm.
    * @brief  Convert time into tm.
    *
    * @param t        时间结构，UTC时间戳
    * @param t        time structure
    */
    static void tm2time(const time_t &t, struct tm &tt);

    /**
    * @brief  time_t转换成tm(不用系统的localtime_r, 否则很慢!!!)
    * @brief  Convert time_t to tm (Don't use system's localtime_r. The function will be slowed down.)
    *
    * @param t        时间结构，UTC时间戳
    * @param t        time structure
    * @param sFormat  需要转换的目标格式，默认为紧凑格式
    * @param sFormat  Target format to be converted, default to compact format
    * @return string  转换后的时间字符串
    * @return string  converted time std::string
    */
    static void tm2tm(const time_t &t, struct tm &stTm);

    /**
	* @brief  获取当前的秒和毫秒
    * @brief  Get the current seconds and milliseconds
	*
	* @param t        时间结构
    * @param t        time structure
	*/
    static int gettimeofday(struct timeval &tv);

    /**
    * @brief  当前时间转换成紧凑格式字符串
    * @brief  Convert current time to compact std::string
    * @param sFormat 格式，默认为紧凑格式
    * @param sFormat the format, default to compact format
    * @return string 转换后的时间字符串
    * @return string converted time string
    */
    static std::string now2str(const std::string &sFormat = "%Y%m%d%H%M%S");

    /**
    * @brief  毫秒时间, 一般用于调试输出
    * @return string 转换后的时间字符串
    */
    static std::string now2msstr();
    static std::string ms2str(int64_t ms);
    /**
    * @brief  时间转换成GMT字符串，GMT格式:Fri, 12 Jan 2001 18:18:18 GMT
    * @brief  Convert time into GMT std::string, GMT格式:Fri, 12 Jan 2001 18:18:18 GMT
    * @param stTm    时间结构
    * @param stTm    time structure
    * @return string GMT格式的时间字符串
    * @return string time std::string in GMT format
    */
    static std::string tm2GMTstr(const struct tm &stTm);

    /**
    * @brief  时间转换成GMT字符串，GMT格式:Fri, 12 Jan 2001 18:18:18 GMT
    * @brief  Convert time into GMT std::string, GMT格式:Fri, 12 Jan 2001 18:18:18 GMT
    * @param stTm    时间结构
    * @param stTm    time structure
    * @return string GMT格式的时间字符串
    * @return string time std::string in GMT format
    */
    static std::string tm2GMTstr(const time_t &t);

    /**
    * @brief  当前时间转换成GMT字符串，GMT格式:Fri, 12 Jan 2001 18:18:18 GMT
    * @brief  Convert current time into GMT std::string, GMT格式:Fri, 12 Jan 2001 18:18:18 GMT
    * @return string GMT格式的时间字符串
    * @return string time std::string in GMT format
    */
    static std::string now2GMTstr();

    /**
    * @brief  当前的日期(年月日)转换成字符串(%Y%m%d).
    * @brief  Get current date(yearmonthday) and convert it into std::string (%Y%m%d).
    *
    * @return string (%Y%m%d)格式的时间字符串
    * @return string time std::string in (%Y%m%d) format
    */
    static std::string nowdate2str();

    /**
    * @brief  当前的时间(时分秒)转换成字符串(%H%M%S).
    * @brief  Get current time(hourminutesecond) and convert it into std::string (%H%M%S).
    *
    * @return string (%H%M%S)格式的时间字符串
    * @return string time std::string in (%H%M%S) format
    */
    static std::string nowtime2str();

    /**
     * @brief  获取当前时间的的毫秒数.
     * @brief  Get the value of milliseconds of current time.
     *
     * @return int64_t 当前时间的的毫秒数
     * @return int64_t current milliseconds of this time
     */
    static int64_t now2ms();

    /**
     * @brief  取出当前时间的微秒.
     * @brief  Take out microseconds of current time.
     *
     * @return int64_t 取出当前时间的微秒
     * @return int64_t Take out microseconds of current time.
     */
    static int64_t now2us();

    /**
    * @brief  字符串转化成T型，如果T是数值类型, 如果str为空,则T为0.
    * @brief  Convert std::string to type T. if T is a numeric type and STR is empty, then T values 0.
    *
    * @param sStr  要转换的字符串
    * @param sStr  the std::string needs to be converted
    * @return T    T型类型
    * @return T    the type of type T
    */
    template<typename T>
    static T strto(const std::string &sStr);

    /**
    * @brief  字符串转化成T型.
    * @brief  Convert std::string to type T
    *
    * @param sStr      要转换的字符串
    * @param sStr      the std::string needs to be converted
    * @param sDefault  缺省值
    * @param sDefault  default value
    * @return T        转换后的T类型
    * @return T        the converted type of type T
    */
    template<typename T>
    static T strto(const std::string &sStr, const std::string &sDefault);

    /**
    * @brief  解析字符串,用分隔符号分隔,保存在vector里
    * @brief  Parse std::string, separate with separator, and save in vector
    * 
    * 例子: |a|b||c|
    * Example: |a|b||c|
    * 
    * 如果withEmpty=true时, 采用|分隔为:"","a", "b", "", "c", ""
    * If 'withEmpty=true' then use '|' to separate it into "","a", "b", "", "c", "".
    *
    * 如果withEmpty=false时, 采用|分隔为:"a", "b", "c"
    * If 'withEmpty=false' then use '|' to separate it into "a", "b", "c".
    *
    * 如果T类型为int等数值类型, 则分隔的字符串为"", 则强制转化为0
    * If the T type is a numeric type such as int, the delimited std::string is' ', then it is forced to 0.
    *
    * @param sStr      输入字符串
    * @param sStr      input std::string
    * @param sSep      分隔字符串(每个字符都算为分隔符)
    * @param sSep      the separator std::string (each character counts as a separator)
    * @param withEmpty true代表空的也算一个元素, false时空的过滤
    * @param withEmpty bool: true, represented that empty is also an element ; false, filter empty ones. 
    * @return          解析后的字符vector
    * @return          parsed character: vector
    */
    template<typename T>
    static std::vector<T> sepstr(const std::string &sStr, const std::string &sSep, bool withEmpty = false);

    /**
    * @brief T型转换成字符串，只要T能够使用ostream对象用<<重载,即可以被该函数支持
    * @brief Convert T-type to std::string. As long as T can use ostream object with << to overload, it can be supported by this function.
    * 
    * @param t 要转换的数据
    * @param t the data needs to be converted
    * @return  转换后的字符串
    * @return  the converted std::string
    */
    template<typename T>
    inline static std::string tostr(const T &t)
    {
        std::ostringstream sBuffer;
        sBuffer << t;
        return sBuffer.str();
    }

    /**
     * @brief  vector转换成string.
     * @brief  Convert vector to std::string.
     *
     * @param t 要转换的vector型的数据
     * @param t data which need to be convertes to vector type
     * @return  转换后的字符串
     * @return  the converted std::string
     */
    template<typename T>
    static std::string tostr(const std::vector<T> &t);

    /**
     * @brief  把map输出为字符串.
     * @brief  export map as std::string
     *
     * @param map<K, V, D, A>  要转换的map对象
     * @param map<K, V, D, A>  the map object needs to be converted
     * @return                    std::string 输出的字符串
     * @return                    output std::string 
     */
    template<typename K, typename V, typename D, typename A>
    static std::string tostr(const std::map<K, V, D, A> &t);

    /**
     * @brief  map输出为字符串.
     * @brief  export map as std::string
     *
     * @param multimap<K, V, D, A>  map对象
     * @param multimap<K, V, D, A>  the map object needs to be converted
     * @return                      输出的字符串
     * @return                      output std::string 
     */
    template<typename K, typename V, typename D, typename A>
    static std::string tostr(const std::multimap<K, V, D, A> &t);

    /**
     * @brief  把map输出为字符串.
     * @brief  export map as std::string
     *
     * @param map<K, V, D, A>  要转换的map对象
     * @param map<K, V, D, A>  the map object needs to be converted
     * @return                    std::string 输出的字符串
     * @return                    output std::string 
     */
    template<typename K, typename V, typename D, typename P, typename A>
    static std::string tostr(const std::unordered_map<K, V, D, P, A> &t);

    /**
    * @brief  pair 转化为字符串，保证map等关系容器可以直接用tostr来输出
    * @brief  Convert pair to std::string, ensure that the relationship containers such as map can output directly with tostr.
    * @param pair<F, S> pair对象
    * @param pair<F, S> object pair
    * @return           输出的字符串
    * @return           output std::string 
    */
    template<typename F, typename S>
    static std::string tostr(const std::pair<F, S> &itPair);

    /**
    * @brief  container 转换成字符串.
    * @brief  Convert container to std::string
    *
    * @param iFirst  迭代器
    * @param iFirst  iterator
    * @param iLast   迭代器
    * @param iLast   iterator
    * @param sSep    两个元素之间的分隔符
    * @param sSep    the separator between two elements
    * @return        转换后的字符串
    * @return        the converted std::string 
    */
    template<typename InputIter>
    static std::string tostr(InputIter iFirst, InputIter iLast, const std::string &sSep = "|");

    /**
    * @brief  二进制数据转换成字符串.
    * @brief  Convert binary data t0 std::string 
    *
    * @param buf     二进制buffer
    * @param buf     binary buffer
    * @param len     buffer长度
    * @param len     buffer length
    * @param sSep    分隔符
    * @param sSep    separator
    * @param lines   多少个字节换一行, 默认0表示不换行
    * @param lines   The max number of bytes for oneline.By default, 0 means no new line.
    * @return        转换后的字符串
    * @return        the converted std::string 
    */
    static std::string bin2str(const void *buf, size_t len, const std::string &sSep = "", size_t lines = 0);

    /**
    * @brief  二进制数据转换成字符串.
    * @brief  Convert binary data t0 std::string 
    *
    * @param sBinData  二进制数据
    * @param sBinData  binary data
    * @param sSep     分隔符
    * @param sSep     separator
    * @param lines    多少个字节换一行, 默认0表示不换行
    * @param lines    The max number of bytes for oneline.By default, 0 means no new line.
    * @return         转换后的字符串
    * @return         the converted std::string
    */
    static std::string bin2str(const std::string &sBinData, const std::string &sSep = "", size_t lines = 0);

    /**
    * @brief   字符串转换成二进制.
    * @brief   Convert std::string to binary
    *
    * @param psAsciiData 字符串
    * @param psAsciiData std::string
    * @param sBinData    二进制数据
    * @param sBinData    binary data
    * @param iBinSize    需要转换的字符串长度
    * @param iBinSize    the length of the std::string which needs to be converted.
    * @return            转换长度，小于等于0则表示失败
    * @return            Conversion length, less than or equal to 0 means failure
    */
    static int str2bin(const char *psAsciiData, unsigned char *sBinData, int iBinSize);

    /**
     * @brief  字符串转换成二进制.
     * @brief  convert std::string to binary
     *
     * @param sBinData  要转换的字符串
     * @param sBinData  the std::string needs to be converted
     * @param sSep      分隔符
     * @param sSep      separator
     * @param lines     多少个字节换一行, 默认0表示不换行
     * @param lines     The max number of bytes for oneline.By default, 0 means no new line.
     * @return          转换后的二进制数据
     * @return          the converted binary data
     */
    static std::string str2bin(const std::string &sBinData, const std::string &sSep = "", size_t lines = 0);

    /**
    * @brief  替换字符串.
    * @brief  replace std::string
    *
    * @param sString  输入字符串
    * @param sString  input std::string
    * @param sSrc     原字符串
    * @param sSrc     the original std::string
    * @param sDest    目的字符串
    * @param sDest    the target std::string
    * @return string  替换后的字符串
    * @return string  the converted std::string
    */
    static std::string replace(const std::string &sString, const std::string &sSrc, const std::string &sDest);

    /**
    * @brief  批量替换字符串.
    * @brief  Batch replace std::string.
    *
    * @param sString  输入字符串
    * @param sString  input std::string
    * @param mSrcDest  map<原字符串,目的字符串>
    * @param mSrcDest  map<original,target>
    * @return string  替换后的字符串
    * @return string  the converted std::string
    */
    static std::string replace(const std::string &sString, const std::map<std::string, std::string>& mSrcDest);

    /**
     * @brief 匹配以.分隔的字符串，pat中*则代表通配符，代表非空的任何字符串
     * s为空, 返回false ，pat为空, 返回true
     * @brief Match std::string separated by '.' And '*' in pat represents wildcard which represents any std::string that is not empty.
     * If s is empty, return false. If pat is empty, return true.
     * @param s    普通字符串
     * @param s    normal std::string
     * @param pat  带*则被匹配的字符串，用来匹配ip地址
     * @param pat  std::string matched with * to match IP address
     * @return     是否匹配成功
     * @return     whether they matches or not
     */
    static bool matchPeriod(const std::string& s, const std::string& pat);

    /**
     * 判断在同一个周期的时间是否相等
     * @param lastDate 日期:%Y%m%d格式
     * @param date 日期:%Y%m%d格式
     * @param period: W(周)/M(月)/Q(季)/S(半年)/Y(年)
     * @return
     */
	static bool matchPeriod(int lastDate, int date, const std::string &period);

     /**
     * @brief  匹配以.分隔的字符串.
     * @brief  Match strings separated by '.'
     *
     * @param s   普通字符串
     * @param s   normal std::string 
     * @param pat vector中的每个元素都是带*则被匹配的字符串，用来匹配ip地址
     * @param pat each elment in this vector means std::string matched with * to match IP address
     * @return    是否匹配成功
     * @return    whether they matches or not
     */
    static bool matchPeriod(const std::string& s, const std::vector<std::string>& pat);

    /**
     * @brief  判断一个数是否为素数.
     * @brief  Determine whether a number is prime or not
     * @param n  需要被判断的数据
     * @param n  the data needs to be determined
     * @return   true代表是素数，false表示非素数
     * @return   true for prime , false for non prime
     */
    static bool isPrimeNumber(size_t n);

#if TARGET_PLATFORM_LINUX || TARGET_PLATFORM_IOS

    /**
     * @brief  daemon
     */
    static void daemon();

    /**
     * @brief  忽略管道异常
     * @brief  Ignore pipe exceptions 
     */
    static void ignorePipe();

    /** 
     * @brief  生成基于16进制字符的随机串
     * @brief  Generating random strings based on hexadecimal characters.
     * @param p            存储随机字符串
     * @param p            store random std::string
     * @param len          字符串大小
     * @param len          std::string length
     */
    static void getRandomHexChars(char* p, unsigned int len);

#endif

    /**
     * @brief  将一个string类型转成一个字节 .
     * @brief  Convert a std::string type to a byte .
     *
     * @param sWhat 要转换的字符串
     * @param sWhat the std::string which needs to be converted
     * @return char    转换后的字节
     * @return char    the converted byte
     */
    static char x2c(const std::string &sWhat);

    /**
     * @brief  大小字符串换成字节数，支持K, M, G两种 例如: 1K, 3M, 4G, 4.5M, 2.3G
     * @brief  The std::string can be changed into bytes. It supports two kinds of K, M and G, such as 1K, 3M, 4G, 4.5M and 2.3G
     * @param s            要转换的字符串
     * @param s            the std::string which needs to be converted
     * @param iDefaultSize 格式错误时, 缺省的大小
     * @param iDefaultSize the default size in case of format error
     * @return             字节数
     * @return             Bytes
     */
    static size_t toSize(const std::string &s, size_t iDefaultSize);

	/**
	* @brief  获取主机名称.
    * @brief  Get machine name
	* @return string    主机名，失败是返回空
    * @return string    machine name. if failed returns null
	*/
	static std::string getHostName();

    //下一天日期, sDate是:%Y%m%d
    static std::string nextDate(const std::string &sDate);
    //上一天日期, sDate是:%Y%m%d
    static std::string prevDate(const std::string &sDate);
    //下一天日期, iDate是:%Y%m%d
    static int nextDate(int iDate);
    //上一天日期, iDate是:%Y%m%d
    static int prevDate(int iDate);

    //下一个月份 sMonth是:%Y%m
    static std::string nextMonth(const std::string &sMonth);
    //上一个月份 sMonth是:%Y%m
    static std::string prevMonth(const std::string &sMonth);
    //下一个年份 sYear是:%Y
    static std::string nextYear(const std::string &sYear);
    //上一个年份 sYear是:%Y
    static std::string prevYear(const std::string &sYear);

    //秒到日期%Y%m%d
    static int secondsToDateInt(time_t seconds);
    //秒到日期%Y%m%d
    static std::string secondsToDateString(time_t seconds);

    //秒到周一%Y%m%d
    static std::string secondsToMondayString(time_t seconds);
    //秒到月 %Y-%m
    static std::string secondsToMonthString(time_t seconds);

    //日期(%Y%m%d)到毫秒
    static int64_t dateToMs(const std::string &sDate);

    static int64_t dateToSecond(const std::string &sDate);

    static int dateTo(const std::string &sDate, const std::string &sPeriod);

    //日期(%Y%m%d)到周几
    static int dateToWeekday(const std::string &sDate);

    //日期(%Y%m%d)到第几周
    static int dateToWeek(const std::string &sDate);

    //日期(%Y%m%d)到x月
    static int dateToMonth(const std::string &sDate);

    //日期(%Y%m%d)到x季度
    static int dateToSeason(const std::string &sDate);

    //日期(%Y%m%d)到上下半年
    static int dateToHalfYear(const std::string &sDate);

    //日期(%Y%m%d)到x年
    static int dateToYear(const std::string &sDate);

    //MS到字符串 %Y%m%d-%H%M%S-xxx
    static std::string msToTimeString(int64_t ms);
    //字符串到MS %Y%m%d-%H%M%S-xxx，转换失败返回0
    static int64_t timeStringToMs(const std::string & timeStr);

    //MS 中取出日期(%Y%m%d)、时间字符串(%H%M%S)、MS字符串(xxx)
    static bool getSectionFromMs(int64_t ms,std::string &date,std::string& time,std::string &mstick);
    //MS 中取出日期(%Y%m%d)，失败返回"00000000"
    static std::string getDateFromMs(int64_t ms);
    //MS 中取出时间(%H%M%S)，失败返回"000000"
    static std::string getTimeFromMs(int64_t ms);

    //毫秒换成当天的秒钟
    static int msToNowSeconds(int64_t ms);
    static int nowDaySeconds();
    //换成当天开始的毫秒
    static int64_t msToNowMs(int64_t ms);

    static int64_t us();

    //当前的小时换成日期秒数 0 ~ (86400-1),hour 0~23 ;min 0~59
    static int64_t timeToDaySec(int64_t hour, int64_t min);

    //获取下一个通知的绝对时间，clockSec为当天换成秒钟
    static int64_t getNextAbsClockMs( int64_t clockSec);

    static int nextDate(int iDate, int offset);
    static int prevDate(int iDate, int offset);
    // 取日期所在的周期中的最后一天
    static int lastDate(int iDate, const char period = 'D');
    // 获取日期列表内和period匹配的日期
    // period: ('W', 1) ('M', -2), ('Q', 5) // 取每周/每月/每季度第几天 (负数则是倒数地几天)
    static int getMatchPeriodDays(const std::vector<int>& days, const std::pair<std::string, int>& period, std::vector<int>& matchDays);

};

namespace p
{
    template<typename D>
    struct strto1
    {
        D operator()(const std::string &sStr)
        {
            std::string s = "0";

            if(!sStr.empty())
            {
                s = sStr;
            }

            std::istringstream sBuffer(s);

            D t;
            sBuffer >> t;

            return t;
        }
    };

    template<>
    struct strto1<char>
    {
        char operator()(const std::string &sStr)
        {
            if(!sStr.empty())
            {
                return sStr[0];
            }
            return 0;
        }
    };

    template<>
    struct strto1<unsigned char>
    {
        unsigned char operator()(const std::string &sStr)
        {
            if(!sStr.empty())
            {
                return (unsigned char)sStr[0];
            }
            return 0;
        }
    };

    template<>
    struct strto1<short>
    {
        short operator()(const std::string &sStr)
        {
            if (!sStr.empty()) {
                if (sStr.find("0x") == 0) {
                    return (short) ::strtol(sStr.c_str(), NULL, 16);
                }
                else {
                    return atoi(sStr.c_str());
                }
            }
            return 0;
        }
    };

    template<>
    struct strto1<unsigned short>
    {
        unsigned short operator()(const std::string &sStr)
        {
            if (!sStr.empty()) {
                if (sStr.find("0x") == 0) {
                    return (unsigned short) ::strtoul(sStr.c_str(), NULL, 16);
                }
                else {
                    return (unsigned short) strtoul(sStr.c_str(), NULL, 10);
                }
            }
            return 0;
        }
    };

    template<>
    struct strto1<int>
    {
        int operator()(const std::string &sStr)
        {
            if (!sStr.empty()) {
                if (sStr.find("0x") == 0) {
                    return ::strtol(sStr.c_str(), NULL, 16);
                }
                else {
                    return atoi(sStr.c_str());
                }
            }
            return 0;
        }
    };

    template<>
    struct strto1<unsigned int>
    {
        unsigned int operator()(const std::string &sStr)
        {
            if (!sStr.empty()) {
                if (sStr.find("0x") == 0) {
                    return ::strtoul(sStr.c_str(), NULL, 16);
                }
                else {
                    return strtoul(sStr.c_str(), NULL, 10);
                }
            }
            return 0;
        }
    };

    template<>
    struct strto1<long>
    {
        long operator()(const std::string &sStr)
        {
            if (!sStr.empty()) {
                if (sStr.find("0x") == 0) {
                    return ::strtol(sStr.c_str(), NULL, 16);
                }
                else {
                    return atol(sStr.c_str());
                }
            }
            return 0;
        }
    };

    template<>
    struct strto1<long long>
    {
        long long operator()(const std::string &sStr)
        {
            if (!sStr.empty()) {
                if (sStr.find("0x") == 0) {
                    return ::strtoll(sStr.c_str(), NULL, 16);
                }
                else {
                    return atoll(sStr.c_str());
                }
            }
            return 0;
        }
    };

    template<>
    struct strto1<unsigned long>
    {
        unsigned long operator()(const std::string &sStr)
        {
            if (!sStr.empty()) {
                if (sStr.find("0x") == 0) {
                    return ::strtoul(sStr.c_str(), NULL, 16);
                }
                else {
                    return strtoul(sStr.c_str(), NULL, 10);
                }
            }
            return 0;
        }
    };

    template<>
    struct strto1<float>
    {
        float operator()(const std::string &sStr)
        {
            if(!sStr.empty())
            {
                return (float) atof(sStr.c_str());
            }
            return 0;
        }
    };

    template<>
    struct strto1<double>
    {
        double operator()(const std::string &sStr)
        {
            if(!sStr.empty())
            {
                return atof(sStr.c_str());
            }
            return 0;
        }
    };

    template<typename D>
    struct strto2
    {
    	D operator()(const std::string &sStr, typename std::enable_if<!std::is_enum<D>::value, void ***>::type dummy = 0)
        {
            std::istringstream sBuffer(sStr);
            D t;

            sBuffer >> t;

            return t;
        }

        D operator()(const std::string &sStr, typename std::enable_if<std::is_enum<D>::value, void ***>::type dummy = 0)
        {
    		std::istringstream sBuffer(sStr);
    		int i;
    		sBuffer >> i;

    		return (D)i;
        }
    };

    template<>
    struct strto2<std::string>
    {
        std::string operator()(const std::string &sStr)
        {
            return sStr;
        }
    };

}

template<typename T>
T TC_Common::strto(const std::string &sStr)
{
    using strto_type = typename std::conditional<std::is_arithmetic<T>::value, p::strto1<T>, p::strto2<T>>::type;

    return strto_type()(sStr);
}

template<typename T>
T TC_Common::strto(const std::string &sStr, const std::string &sDefault)
{
    std::string s;

    if(!sStr.empty())
    {
        s = sStr;
    }
    else
    {
        s = sDefault;
    }

    return strto<T>(s);
}

template<typename T>
std::vector<T> TC_Common::sepstr(const std::string &sStr, const std::string &sSep, bool withEmpty)
{
    std::vector<T> vt;

    std::string::size_type pos = 0;
    std::string::size_type pos1 = 0;

    while (true) {
        std::string s;
        pos1 = sStr.find_first_of(sSep, pos);
        if (pos1 == std::string::npos) {
            if (pos + 1 <= sStr.length()) {
                s = sStr.substr(pos);
            }
        }
        else if (pos1 == pos) {
            s = "";
        }
        else {
            s = sStr.substr(pos, pos1 - pos);
            pos = pos1;
        }

        if (withEmpty) {
            vt.push_back(std::move(strto<T>(s)));
        }
        else {
            if (!s.empty()) {
                vt.push_back(std::move(strto<T>(s)));
            }
        }

        if (pos1 == std::string::npos) {
            break;
        }

        pos++;
    }

    return vt;
}

template<>
std::string TC_Common::tostr<bool>(const bool &t);

template<>
std::string TC_Common::tostr<char>(const char &t);

template<>
std::string TC_Common::tostr<unsigned char>(const unsigned char &t);

template<>
std::string TC_Common::tostr<short>(const short &t);

template<>
std::string TC_Common::tostr<unsigned short>(const unsigned short &t);

template<>
std::string TC_Common::tostr<int>(const int &t);

template<>
std::string TC_Common::tostr<unsigned int>(const unsigned int &t);

template<>
std::string TC_Common::tostr<long>(const long &t);

template<>
std::string TC_Common::tostr<long long>(const long long &t);

template<>
std::string TC_Common::tostr<unsigned long>(const unsigned long &t);

template<>
std::string TC_Common::tostr<float>(const float &t);

template<>
std::string TC_Common::tostr<double>(const double &t);

template<>
std::string TC_Common::tostr<long double>(const long double &t);

template<>
std::string TC_Common::tostr<std::string>(const std::string &t);

template<typename T>
std::string TC_Common::tostr(const std::vector<T> &t)
{
    std::string s;
    for(size_t i = 0; i < t.size(); i++)
    {
        s += tostr(t[i]);
        s += " ";
    }
    return s;
}

template<typename K, typename V, typename D, typename A>
std::string TC_Common::tostr(const std::map<K, V, D, A> &t)
{
    std::string sBuffer;
    typename std::map<K, V, D, A>::const_iterator it = t.begin();
    while(it != t.end())
    {
        sBuffer += " [";
        sBuffer += tostr(it->first);
        sBuffer += "]=[";
        sBuffer += tostr(it->second);
        sBuffer += "] ";
        ++it;
    }
    return sBuffer;
}

template<typename K, typename V, typename D, typename A>
std::string TC_Common::tostr(const std::multimap<K, V, D, A> &t)
{
    std::string sBuffer;
    typename std::multimap<K, V, D, A>::const_iterator it = t.begin();
    while(it != t.end())
    {
        sBuffer += " [";
        sBuffer += tostr(it->first);
        sBuffer += "]=[";
        sBuffer += tostr(it->second);
        sBuffer += "] ";
        ++it;
    }
    return sBuffer;
}

template<typename K, typename V, typename D, typename P, typename A>
std::string TC_Common::tostr(const std::unordered_map<K, V, D, P, A> &t)
{
    std::string sBuffer;
    typename std::unordered_map<K, V, D, P, A>::const_iterator it = t.begin();
    while (it != t.end()) {
        sBuffer += " [";
        sBuffer += tostr(it->first);
        sBuffer += "]=[";
        sBuffer += tostr(it->second);
        sBuffer += "] ";
        ++it;
    }
    return sBuffer;
}

template<typename F, typename S>
std::string TC_Common::tostr(const std::pair<F, S> &itPair)
{
    std::string sBuffer;
    sBuffer += "[";
    sBuffer += tostr(itPair.first);
    sBuffer += "]=[";
    sBuffer += tostr(itPair.second);
    sBuffer += "]";
    return sBuffer;
}

template<typename InputIter>
std::string TC_Common::tostr(InputIter iFirst, InputIter iLast, const std::string &sSep)
{
    std::string sBuffer;
    InputIter it = iFirst;

    while(it != iLast)
    {
        sBuffer += tostr(*it);
        ++it;

        if(it != iLast)
        {
            sBuffer += sSep;
        }
        else
        {
            break;
        }
    }

    return sBuffer;
}

template<typename V,typename E>
bool TC_Common::equal(const V& x, const V& y,E eps)
{
    return x == y;
}

template<typename K, typename V, typename D, typename A, typename E>
bool TC_Common::equal(const std::map<K, V, D, A>& mx, const std::map<K, V, D, A>& my, E epsilon)
{
    auto first1= mx.begin();
    auto last1 = mx.end();
    auto first2 = my.begin();
    auto last2 = my.end();

    if (distance(first1, last1) != distance(first2, last2))
    {
        return false;
    }

    bool doubleKey = (std::is_same<K, double>::value || std::is_same<K, float>::value);
    bool doubleValue = (std::is_same<V, double>::value || std::is_same<V, float>::value);

    for (; first2 != last2; ++first1, ++first2)
    {
            if (doubleKey )
            {
                if (!TC_Common::equal(first1->first ,first2->first, epsilon) )
                {
                    return false;
                }
            }
            else
            {
                if (first1->first != first2->first)
                {
                    return false;
                }
            }

            if (doubleValue)
            {
                if (!TC_Common::equal(first1->second, first2->second, epsilon))
                {
                    return false;
                }
            }
            else
            {
                if ( first1->second != first2->second)
                {
                    return false;
                }
            }
    }
    return true;
}

template<typename K, typename V, typename D, typename A , typename E>
bool TC_Common::equal(const std::unordered_map<K, V, D, A>& mx , const std::unordered_map<K, V, D, A>& my, E epsilon)
{
	auto first1= mx.begin();
	auto last1 = mx.end();
	auto first2 = my.begin();
	auto last2 = my.end();

	if (distance(first1, last1) != distance(first2, last2))
	{
		return false;
	}

	bool doubleKey = (std::is_same<K, double>::value || std::is_same<K, float>::value);
	bool doubleValue = (std::is_same<V, double>::value || std::is_same<V, float>::value);

	for (; first2 != last2; ++first1, ++first2)
	{
		if (doubleKey )
		{
			if (!TC_Common::equal(first1->first ,first2->first, epsilon) )
			{
				return false;
			}
		}
		else
		{
			if (first1->first != first2->first)
			{
				return false;
			}
		}

		if (doubleValue)
		{
			if (!TC_Common::equal(first1->second, first2->second, epsilon))
			{
				return false;
			}
		}
		else
		{
			if ( first1->second != first2->second)
			{
				return false;
			}
		}
	}
	return true;
}

#if TARGET_PLATFORM_WINDOWS
#define __filename__(x) (strrchr(x, '\\') ? strrchr(x, '\\') + 1 : x)
#define FILE_FUNC_LINE "[" << __filename__(__FILE__) << "::" << __FUNCTION__ << "::" << __LINE__ << "]"
#else
#define FILE_FUNC_LINE "[" << __FILE__ << "::" << __FUNCTION__ << "::" << __LINE__ << "]"
#endif

}

#endif
