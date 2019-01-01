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
#include <map>
namespace dev
{
namespace initializer
{
class LogInitializer
{
public:
    typedef std::shared_ptr<LogInitializer> Ptr;
    LogInitializer() {}
    void initLog(boost::property_tree::ptree const& _pt);
    static void inline logRotateByTime()
    {
        if (std::chrono::system_clock::now() <= nextWakeUp)
            return;
        nextWakeUp = std::chrono::system_clock::now() + wakeUpDelta;
        auto L = el::Loggers::getLogger("default");
        if (L == nullptr)
        {
            INITIALIZER_LOG(ERROR) << "Oops, it is not called default!";
        }
        else
        {
            L->reconfigure();
        }

        L = el::Loggers::getLogger("fileLogger");
        if (L == nullptr)
        {
            INITIALIZER_LOG(ERROR) << "Oops, it is not called fileLogger!";
        }
        else
        {
            L->reconfigure();
        }
    }

private:
    static const std::chrono::seconds wakeUpDelta;
    static std::chrono::system_clock::time_point nextWakeUp;
};
}  // namespace initializer
}  // namespace dev
