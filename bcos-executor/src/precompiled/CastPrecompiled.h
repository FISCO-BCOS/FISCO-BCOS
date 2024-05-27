#pragma once

#include "../vm/Precompiled.h"
#include "bcos-executor/src/precompiled/common/Common.h"
#include <bcos-crypto/interfaces/crypto/CommonType.h>

namespace bcos::precompiled
{
class CastPrecompiled : public Precompiled
{
public:
    using Ptr = std::shared_ptr<CastPrecompiled>;
    CastPrecompiled(crypto::Hash::Ptr _hashImpl);
    virtual ~CastPrecompiled() = default;

    std::shared_ptr<PrecompiledExecResult> call(
        std::shared_ptr<executor::TransactionExecutive> _executive,
        PrecompiledExecResult::Ptr _callParameters) override;
        
};
}  // namespace precompiled