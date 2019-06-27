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
 * @brief
 *
 * @file Exceptions.cpp
 * @author: yujiechen
 * @date 2018-08-24
 */
#include <iostream>

#include <libdevcore/Assertions.h>
#include <libdevcore/Exceptions.h>
#include <test/tools/libutils/TestOutputHelper.h>
#include <boost/test/unit_test.hpp>
#include <memory>

using namespace dev;

namespace dev
{
namespace test
{
BOOST_FIXTURE_TEST_SUITE(ExceptionsTests, TestOutputHelperFixture)

/**
 * @brief : throw exception, and then check the error information
 *
 * @tparam E : type of error_info
 * @tparam T : data type of error_info encapsulated
 * @param error_info : detailed error information
 */
template <typename E, typename T>
void checkErrorInfo(const T& error_info)
{
    try
    {
        boost::throw_exception(Exception() << E(error_info));
    }
    catch (const Exception& e)
    {
        BOOST_CHECK(*(boost::get_error_info<E>(e)) == error_info);
    }
}

/// test exceptions
BOOST_AUTO_TEST_CASE(testExceptions)
{
    BOOST_CHECK_THROW(assertThrow(false, Exception, "Throw Exception"), Exception);
    BOOST_CHECK_THROW(assertThrow(false, RLPException, "Throw RlpException"), RLPException);
    BOOST_CHECK_THROW(assertThrow(false, BadCast, "Thrown RLP BadCast Exception"), BadCast);
    BOOST_CHECK_THROW(assertThrow(false, BadRLP, "Throw RLP BadRLP Exception"), BadRLP);
    BOOST_CHECK_THROW(assertThrow(false, OversizeRLP, "Throw OversizeRLP Exception"), OversizeRLP);
    BOOST_CHECK_THROW(
        assertThrow(false, UndersizeRLP, "Throw UndersizeRLP Exception"), UndersizeRLP);
    BOOST_CHECK_THROW(
        assertThrow(false, BadHexCharacter, "Throw BadHexCharacter Exception"), BadHexCharacter);
    BOOST_CHECK_THROW(
        assertThrow(false, NoNetworking, "Throw NoNetworking Exception"), NoNetworking);
    BOOST_CHECK_THROW(
        assertThrow(false, RootNotFound, "Throw RootNotFound Exception"), RootNotFound);
    BOOST_CHECK_THROW(assertThrow(false, BadRoot, "Throw BadRoot Exception"), BadRoot);
    BOOST_CHECK_THROW(assertThrow(false, FileError, "Throw FileError Exception"), FileError);
    BOOST_CHECK_THROW(assertThrow(false, Overflow, "Throw Overflow Exception"), Overflow);
    BOOST_CHECK_THROW(
        assertThrow(false, FailedInvariant, "Throw FailedInvariant Exception"), FailedInvariant);
    BOOST_CHECK_THROW(
        assertThrow(false, InterfaceNotSupported, "Throw InterfaceNotSupported Exception"),
        InterfaceNotSupported);
    BOOST_CHECK_THROW(
        assertThrow(false, ExternalFunctionFailure, "Throw ExternalFunctionFailure Exception"),
        ExternalFunctionFailure);
    BOOST_CHECK_THROW(assertThrow(false, InitLedgerConfigFailed, "Init ledger config failed"),
        InitLedgerConfigFailed);
    BOOST_CHECK_THROW(assertThrow(false, OpenDBFailed, "OpenDBFailed"), OpenDBFailed);
}

/// test error_info
BOOST_AUTO_TEST_CASE(testErrorInfos)
{
    checkErrorInfo<errinfo_invalidSymbol, const char>('c');
    checkErrorInfo<errinfo_wrongAddress, std::string>("wrong address");
    checkErrorInfo<errinfo_comment, std::string>("error with comment");
    checkErrorInfo<errinfo_required, bigint>((bigint)(12310000000000));
    checkErrorInfo<errinfo_got, bigint>((bigint)(12310000000000));
    checkErrorInfo<errinfo_min, bigint>((bigint)(3000000));
    checkErrorInfo<errinfo_max, bigint>((bigint)(12310000000000));
    checkErrorInfo<errinfo_hash256, h256>(FixedHash<32u>("0x0012323"));
    checkErrorInfo<errinfo_required_h256>(FixedHash<32u>("0x1232324324"));
    checkErrorInfo<errinfo_got_h256, h256>(FixedHash<32u>("0x1234567890abcdef"));
    checkErrorInfo<errinfo_extraData, bytes>(std::vector<unsigned char>(10, 'a'));
    checkErrorInfo<errinfo_externalFunction, char const*>("abcd");
    checkErrorInfo<errinfo_interface, std::string>("ERROR Interface");
    checkErrorInfo<errinfo_path, std::string>("Error Path");
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace test
}  // namespace dev
