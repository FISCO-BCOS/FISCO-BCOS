#pragma once

#include <bcos-framework/interfaces/ledger/LedgerInterface.h.h>
#include <boost/test/unit_test.hpp>

namespace bcos::test
{
class MockLedger : public bcos::ledger::LedgerInterface
{
}
}  // namespace bcos::test