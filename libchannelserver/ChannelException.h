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
 * @file: ChannelException.h
 * @author: monan
 * @date: 2017
 */

#pragma once

#include <string>
#define CHANNEL_LOG(LEVEL) LOG(LEVEL) << "[CHANNEL]"
#define CHANNEL_SESSION_LOG(LEVEL) LOG(LEVEL) << "[CHANNEL][SESSION]"
#pragma warning(push)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include <boost/exception/diagnostic_information.hpp>
#pragma warning(pop)
#pragma GCC diagnostic pop
namespace bcos
{
namespace channel
{
class ChannelException : public std::exception
{
public:
    ChannelException(){};
    ChannelException(int _errorCode, const std::string& _msg)
      : m_errorCode(_errorCode), m_msg(_msg){};

    virtual int errorCode() { return m_errorCode; };
    virtual const char* what() const noexcept override { return m_msg.c_str(); };

    bool operator!() const { return m_errorCode == 0; }

private:
    int m_errorCode = 0;
    std::string m_msg = "";
};

}  // namespace channel

}  // namespace bcos
