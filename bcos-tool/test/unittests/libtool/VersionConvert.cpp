
#include <bcos-tool/VersionConverter.h>
#include <bcos-utilities/Common.h>
#include <boost/test/tools/old/interface.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::tool;

namespace bcos
{
namespace test
{
BOOST_AUTO_TEST_CASE(testVersionConvert)
{
    BOOST_CHECK_EQUAL(toVersionNumber("3.2.3"), 0x03020300);
    BOOST_CHECK_EQUAL(toVersionNumber("3.2"), 0x03020000);
    BOOST_CHECK_THROW(toVersionNumber("1"), InvalidVersion);
    BOOST_CHECK_THROW(toVersionNumber("2.1"), InvalidVersion);
    BOOST_CHECK_THROW(toVersionNumber("256.1"), InvalidVersion);
}

}  // namespace test
}  // namespace bcos