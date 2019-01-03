/**
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
 *
 * @brief: setting log
 *
 * @file: LogInitializer.h
 * @author: yujiechen
 * @date 2018-11-07
 */
#pragma once
#include "Common.h"
#include <libdevcore/easylog.h>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/expressions/formatters/named_scope.hpp>
#include <boost/log/sinks/async_frontend.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/shared_ptr.hpp>
namespace dev
{
namespace initializer
{
class LogInitializer
{
public:
    typedef std::shared_ptr<LogInitializer> Ptr;
    typedef boost::log::sinks::asynchronous_sink<boost::log::sinks::text_file_backend> sink_t;
    virtual ~LogInitializer() { stopLogging(); }
    LogInitializer() {}
    void initLog(boost::property_tree::ptree const& _pt,
        std::string const& channel = dev::FileLogger, std::string const& logType = "log");
    void stopLogging();
    unsigned getLogLevel(std::string const& levelStr);
    static void inline logRotateByTime(){};
    static int m_currentHour;
    static int m_index;

private:
    void stopLogging(boost::shared_ptr<sink_t> sink);
    std::vector<boost::shared_ptr<sink_t>> m_sinks;
};
}  // namespace initializer
}  // namespace dev
