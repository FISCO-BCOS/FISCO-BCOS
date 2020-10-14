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
 *
 * @file StorageException.h
 * @author: mingzhenliu
 * @date 2018-09-13
 */
#pragma once
#include <libdevcore/Exceptions.h>
#include <exception>
#include <string>

namespace dev
{
namespace storage
{
class StorageException : public dev::Exception
{
public:
    StorageException(int errorCode, const std::string& what)
      : dev::Exception(what), m_errorCode(errorCode)
    {}

    virtual int errorCode() const { return m_errorCode; }

private:
    int m_errorCode = 0;
};

}  // namespace storage

}  // namespace dev
