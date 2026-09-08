#pragma once

// Boost.Test print_log_value specializations for the types the opstack suites compare with
// BOOST_CHECK_EQUAL et al. — keeps the rich failure output (both operands printed) that a
// predicate-form BOOST_CHECK(a == b) would lose. Boost requires operator<< (or this
// customization point) for every printed operand; gtest printed these via its universal
// fallback, so the port needs the explicit forms.

#include <boost/test/unit_test.hpp>

#include <bcos-evm/opstack/OpForkSchedule.h>
#include <bcos-evm/eth/state/transaction.hpp>
#include <evmc/evmc.hpp>
#include <evmc/hex.hpp>
#include <intx/intx.hpp>
#include <ostream>
#include <system_error>

namespace boost::test_tools::tt_detail
{
template <>
struct print_log_value<intx::uint256>
{
    void operator()(std::ostream& ostr, const intx::uint256& value)
    {
        ostr << intx::to_string(value);
    }
};

template <>
struct print_log_value<evmc::address>
{
    void operator()(std::ostream& ostr, const evmc::address& value)
    {
        ostr << evmc::hex({value.bytes, sizeof(value.bytes)});
    }
};

template <>
struct print_log_value<evmc::bytes32>
{
    void operator()(std::ostream& ostr, const evmc::bytes32& value)
    {
        ostr << evmc::hex({value.bytes, sizeof(value.bytes)});
    }
};

template <>
struct print_log_value<evmc::bytes>
{
    void operator()(std::ostream& ostr, const evmc::bytes& value)
    {
        ostr << evmc::hex({value.data(), value.size()});
    }
};

template <>
struct print_log_value<bcos::evm::opstack::OpFork>
{
    void operator()(std::ostream& ostr, bcos::evm::opstack::OpFork value)
    {
        ostr << static_cast<int>(value);
    }
};

template <>
struct print_log_value<evmone::state::Transaction::Type>
{
    void operator()(std::ostream& ostr, evmone::state::Transaction::Type value)
    {
        ostr << static_cast<int>(value);
    }
};

template <>
struct print_log_value<std::errc>
{
    void operator()(std::ostream& ostr, std::errc value) { ostr << static_cast<int>(value); }
};
}  // namespace boost::test_tools::tt_detail
