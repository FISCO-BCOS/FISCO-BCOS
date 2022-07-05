#pragma once

#include "../executor/TransactionExecutor.h"
#include <tbb/concurrent_unordered_map.h>
#include <atomic>
#include <stack>

namespace bcos
{
namespace executor
{
class BlockContext;
class TransactionExecutive;

class ExecutiveFactory
{
public:
    using Ptr = std::shared_ptr<ExecutiveFactory>;


    ExecutiveFactory(std::shared_ptr<BlockContext> blockContext,
        std::shared_ptr<std::map<std::string, std::shared_ptr<PrecompiledContract>>>
            precompiledContract,
        std::shared_ptr<std::map<std::string, std::shared_ptr<precompiled::Precompiled>>>
            constantPrecompiled,
        std::shared_ptr<const std::set<std::string>> builtInPrecompiled,
        std::shared_ptr<wasm::GasInjector> gasInjector)
      : m_precompiledContract(precompiledContract),
        m_constantPrecompiled(constantPrecompiled),
        m_builtInPrecompiled(builtInPrecompiled),
        m_blockContext(blockContext),
        m_gasInjector(gasInjector)
    {}
    virtual std::shared_ptr<TransactionExecutive> build(
        const std::string& _contractAddress, int64_t contextID, int64_t seq);


private:
    std::shared_ptr<std::map<std::string, std::shared_ptr<PrecompiledContract>>>
        m_precompiledContract;
    std::shared_ptr<std::map<std::string, std::shared_ptr<precompiled::Precompiled>>>
        m_constantPrecompiled;
    std::shared_ptr<const std::set<std::string>> m_builtInPrecompiled;
    std::shared_ptr<BlockContext> m_blockContext;
    std::shared_ptr<wasm::GasInjector> m_gasInjector;
};

}  // namespace executor
}  // namespace bcos