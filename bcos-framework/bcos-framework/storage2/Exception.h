#pragma once

#include <bcos-utilities/Error.h>

namespace bcos::storage2
{
// clang-format off
struct UnmatchKeyEntries: public bcos::Error {};
struct TableExists: public bcos::Error {};
//clang-format on
}  // namespace bcos::storage2