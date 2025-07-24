#include "TxPoolConfig.h"

bcos::txpool::TxPoolConfig::TxPoolConfig(TxValidatorInterface::Ptr _txValidator,
    bcos::protocol::TransactionSubmitResultFactory::Ptr _txResultFactory,
    bcos::protocol::BlockFactory::Ptr _blockFactory,
    std::shared_ptr<bcos::ledger::LedgerInterface> _ledger,
    NonceCheckerInterface::Ptr _txpoolNonceChecker, int64_t _blockLimit, size_t _poolLimit,
    bool checkTransactionSignature)
  : m_txValidator(std::move(_txValidator)),
    m_txResultFactory(std::move(_txResultFactory)),
    m_blockFactory(std::move(_blockFactory)),
    m_ledger(std::move(_ledger)),
    m_txPoolNonceChecker(std::move(_txpoolNonceChecker)),
    m_blockLimit(_blockLimit),
    m_poolLimit(_poolLimit),
    m_checkTransactionSignature(checkTransactionSignature)
{}
void bcos::txpool::TxPoolConfig::setPoolLimit(size_t _poolLimit)
{
    m_poolLimit = _poolLimit;
}
size_t bcos::txpool::TxPoolConfig::poolLimit() const
{
    return m_poolLimit;
}
bcos::txpool::NonceCheckerInterface::Ptr bcos::txpool::TxPoolConfig::txPoolNonceChecker()
{
    return m_txPoolNonceChecker;
}
bcos::txpool::TxValidatorInterface::Ptr bcos::txpool::TxPoolConfig::txValidator()
{
    return m_txValidator;
}
bcos::protocol::TransactionSubmitResultFactory::Ptr bcos::txpool::TxPoolConfig::txResultFactory()
{
    return m_txResultFactory;
}
bcos::protocol::BlockFactory::Ptr bcos::txpool::TxPoolConfig::blockFactory()
{
    return m_blockFactory;
}
void bcos::txpool::TxPoolConfig::setBlockFactory(bcos::protocol::BlockFactory::Ptr _blockFactory)
{
    m_blockFactory = std::move(_blockFactory);
}
bcos::protocol::TransactionFactory::Ptr bcos::txpool::TxPoolConfig::txFactory()
{
    return m_blockFactory->transactionFactory();
}
std::shared_ptr<bcos::ledger::LedgerInterface> bcos::txpool::TxPoolConfig::ledger()
{
    return m_ledger;
}
int64_t bcos::txpool::TxPoolConfig::blockLimit() const
{
    return m_blockLimit;
}
bool bcos::txpool::TxPoolConfig::checkTransactionSignature() const
{
    return m_checkTransactionSignature;
}
