#pragma once
#include "../Common.h"
#include <bcos-framework/concepts/Hash.h>
#include <bcos-tars-protocol/tars/Transaction.h>
#include <string>
#include <vector>

namespace bcos::concepts::hash
{

template <bcos::crypto::hasher::Hasher Hasher>
auto calculate(bcostars::Transaction const& transaction)
{}

}  // namespace bcos::concepts::hash
