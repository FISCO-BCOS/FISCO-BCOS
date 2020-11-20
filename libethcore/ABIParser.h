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
 * @brief Contract ABI function signature parser tool.
 * @author: octopuswang
 * @date: 2019-04-01
 */

#pragma once

#include <libdevcrypto/CryptoInterface.h>
#include <libutilities/Address.h>
#include <libutilities/Common.h>
#include <libutilities/CommonData.h>
#include <boost/algorithm/string.hpp>

namespace bcos
{
namespace eth
{
namespace abi
{
enum class ABI_ELEMENTARY_TYPE
{             // solidity ABI  elementary types
    INVALID,  // invalid
    BOOL,     // bool
    INT,      // int8  ~ int256
    UINT,     // uint8 ~ uint256
    BYTESN,   // bytesN
    ADDR,     // address
    BYTES,    // bytes
    STRING,   // string
    FIXED,    // fixed, unsupport
    UNFIXED   // unfixed, unsupport
};

class ABIInType
{
public:
    ABIInType() = default;

    bool reset(const std::string& _str);
    void clear();

public:
    // the number of dimensions of T or zero
    std::size_t rank() { return extents.size(); }
    // obtains the size of an array type along a specified dimension
    std::size_t extent(std::size_t index) { return index > rank() ? 0 : extents[index - 1]; }
    bool removeExtent();
    bool dynamic();
    bool valid() { return aet != ABI_ELEMENTARY_TYPE::INVALID; }
    std::string getType() const { return strType; }
    std::string getEleType() const { return strEleType; }

    // get abi elementary type by string
    ABI_ELEMENTARY_TYPE getEnumType(const std::string& _strType);

private:
    ABI_ELEMENTARY_TYPE aet{ABI_ELEMENTARY_TYPE::INVALID};
    std::string strEleType;
    std::string strType;
    std::vector<std::size_t> extents;
};

using ABIOutType = ABIInType;

class ABIFunc
{
private:
    std::string strFuncName;
    std::string strFuncSignature;
    std::vector<ABIInType> allParamsType;

public:
    // parser contract abi function signature, eg: transfer(string,string,uint256)
    bool parser(const std::string& _sig);

public:
    std::vector<std::string> getParamsType() const;
    inline std::string getSignature() const { return strFuncSignature; }
    inline std::string getFuncName() const { return strFuncName; }
};

}  // namespace abi
}  // namespace eth
}  // namespace bcos
