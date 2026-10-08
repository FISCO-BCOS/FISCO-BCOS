// Printers for BOOST.Test failure messages on types that have no operator<<.
// BOOST_CHECK_EQUAL on bcos::u256/h256/bytes needs these to produce readable
// output when the comparison fails; without them the framework falls back to
// an opaque "see operator<<" placeholder.
#pragma once

#include <bcos-utilities/Common.h>

#include <boost/test/unit_test.hpp>

#include <sstream>
#include <string>

namespace bcos
{
// bcos::u256 and friends have .str()/hexPrefixed() but no stream output; teach
// BOOST.Test to print them via their existing string forms.
inline std::ostream& operator<<(std::ostream& out, bcos::u256 const& value)
{
    return out << value.str();
}

inline std::ostream& operator<<(std::ostream& out, bcos::s256 const& value)
{
    return out << value.str();
}

inline std::ostream& operator<<(std::ostream& out, bcos::h256 const& value)
{
    return out << value.hex();
}

inline std::ostream& operator<<(std::ostream& out, bcos::h160 const& value)
{
    return out << value.hex();
}
}  // namespace bcos
