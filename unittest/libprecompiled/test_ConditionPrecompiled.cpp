#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libstorage/ConditionPrecompiled.h>
#include <boost/test/unit_test.hpp>
#include "unittest/Common.h"

using namespace dev;
using namespace dev::precompiled;

namespace test_ConditionPrecompiled {

class MockPrecompiledEngine : public PrecompiledContext {
 public:
  virtual ~MockPrecompiledEngine() {}
};

struct ConditionPrecompiledFixture {
  ConditionPrecompiledFixture() {
    context = std::make_shared<MockPrecompiledEngine>();
    BlockInfo blockInfo{h256(0x001), u256(1)};
    context->setBlockInfo(blockInfo);
    conditionPrecompiled = std::make_shared<ConditionPrecompiled>();
    auto condition = std::make_shared<storage::Condition>();
    conditionPrecompiled->setPrecompiledEngine(context);
    conditionPrecompiled->setCondition(condition);
  }

  ~ConditionPrecompiledFixture() {}
  ConditionPrecompiled::Ptr conditionPrecompiled;
  PrecompiledContext::Ptr context;
};

BOOST_FIXTURE_TEST_SUITE(ConditionPrecompiled, ConditionPrecompiledFixture)
BOOST_AUTO_TEST_CASE(beforeBlock) {
  conditionPrecompiled->beforeBlock(context);
}

BOOST_AUTO_TEST_CASE(afterBlock) {
  conditionPrecompiled->afterBlock(context, false);
}

BOOST_AUTO_TEST_CASE(toString) {
  BOOST_TEST(conditionPrecompiled->toString(context) == "Condition");
}

BOOST_AUTO_TEST_CASE(getCondition) { conditionPrecompiled->getCondition(); }

BOOST_AUTO_TEST_CASE(call) {

  eth::ContractABI abi;
  bytes in = abi.abiIn("EQ(string,int256)", "price", 256);
  conditionPrecompiled->call(context, bytesConstRef(&in));
  in = abi.abiIn("EQ(string,string)", "item", "spaceship");
  conditionPrecompiled->call(context, bytesConstRef(&in));
  in = abi.abiIn("GE(string,int256)", "price", 256);
  conditionPrecompiled->call(context, bytesConstRef(&in));
  in = abi.abiIn("GT(string,int256)", "price", 256);
  conditionPrecompiled->call(context, bytesConstRef(&in));
  in = abi.abiIn("LE(string,int256)", "price", 256);
  conditionPrecompiled->call(context, bytesConstRef(&in));
  in = abi.abiIn("LT(string,int256)", "price", 256);
  conditionPrecompiled->call(context, bytesConstRef(&in));
  conditionPrecompiled->call(context, bytesConstRef(&in));
  in = abi.abiIn("NE(string,int256)", "price", 256);
  conditionPrecompiled->call(context, bytesConstRef(&in));
  in = abi.abiIn("NE(string,string)", "name", "WangWu");
  conditionPrecompiled->call(context, bytesConstRef(&in));
  in = abi.abiIn("limit(int256)", u256(123));
  conditionPrecompiled->call(context, bytesConstRef(&in));
  in = abi.abiIn("limit(int256,int256)", u256(2), u256(3));
  conditionPrecompiled->call(context, bytesConstRef(&in));
}

BOOST_AUTO_TEST_SUITE_END()

}  // namespace test_ConditionPrecompiled
