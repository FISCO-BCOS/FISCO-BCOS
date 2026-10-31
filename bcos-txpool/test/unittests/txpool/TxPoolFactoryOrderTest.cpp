/**
 *  Copyright (C) 2026 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 */
// TxPoolFactory::setLedgerConfigState has an ordering precondition -- before createTxPool --
// because the validator takes the holder at construction. A late call must fail where it is
// made instead of leaving admission on a holder nobody publishes into.

#include "TxPoolFixture.h"
#include <bcos-txpool/TxPoolFactory.h>
#include <bcos-utilities/Exceptions.h>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/test/unit_test.hpp>

using namespace bcos;
using namespace bcos::txpool;

namespace bcos::test
{
BOOST_FIXTURE_TEST_SUITE(TxPoolFactoryOrderTest, TxPoolFixture)

BOOST_AUTO_TEST_CASE(holderSetAfterThePoolIsBuiltIsRefused)
{
    auto factory = std::make_shared<TxPoolFactory>(m_nodeId, m_cryptoSuite, m_txResultFactory,
        m_blockFactory, m_frontService, m_ledger, m_groupId, m_chainId, m_blockLimit,
        bcos::txpool::DEFAULT_POOL_LIMIT, true);
    // Before the pool exists: accepted, the order TxPoolInitializer uses.
    BOOST_CHECK_NO_THROW(factory->setLedgerConfigState(m_ledgerConfigState));
    auto txpool = factory->createTxPool(*ioServicePool->getIOService(), ioServicePool);
    BOOST_REQUIRE(txpool);
    // After: the pool's validator holds the first holder and cannot switch. The message is
    // pinned too, so the case cannot be satisfied by some other InvalidParameter on the path.
    BOOST_CHECK_EXCEPTION(
        factory->setLedgerConfigState(std::make_shared<ledger::LedgerConfigState>()),
        InvalidParameter, [](InvalidParameter const& e) {
            return boost::diagnostic_information(e).find("must precede createTxPool") !=
                   std::string::npos;
        });
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace bcos::test
